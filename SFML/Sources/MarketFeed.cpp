// winsock2 must come before any windows.h that other headers might pull in
#include <winsock2.h>
#include <ws2tcpip.h>

#include "../Headers/MarketFeed.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <ctime>
#include <iomanip>
#include <sstream>

#include <nlohmann/json.hpp>

#pragma comment(lib, "ws2_32.lib")

using json = nlohmann::json;


// The feed reports silver scaled by 10000.
static const long long PRICE_SCALE = 10000;

// Item ids start with the tier: "T4_...". A few (UNIQUE_HIDEOUT) have none.
static int TierFromId(const string& id) {
	if (id.size() < 2 || (id[0] != 'T' && id[0] != 't')) {
		return 0;
	}
	int tier = 0;
	size_t i = 1;
	while (i < id.size() && isdigit((unsigned char)id[i])) {
		tier = tier * 10 + (id[i] - '0');
		++i;
	}
	// must be followed by a separator, so "TRASH_..." is not read as tier 0
	if (i == 1 || (i < id.size() && id[i] != '_')) {
		return 0;
	}
	return tier;
}

static string StripEnchantSuffix(const string& id) {
	size_t at = id.find('@');
	return at == string::npos ? id : id.substr(0, at);
}


// ---------------------------------------------------------------- ItemTable

bool ItemTable::Load(const string& path) {
	ifstream fin(path);
	if (!fin) {
		cout << "ItemTable: cannot open " << path << '\n';
		return false;
	}

	// the generated table is ~5000 rows; sizing up front avoids rehashing
	byId.reserve(6000);

	string line;
	while (getline(fin, line)) {
		if (line.empty()) {
			continue;
		}
		size_t tab = line.find('\t');
		if (tab == string::npos || tab == 0) {
			continue;
		}

		// trailing \r if the file ever gets CRLF line endings
		size_t end = line.size();
		while (end > tab + 1 && (line[end - 1] == '\r' || line[end - 1] == '\n')) {
			--end;
		}
		if (end <= tab + 1) {
			continue;
		}

		byId.emplace(line.substr(0, tab), line.substr(tab + 1, end - tab - 1));
	}

	cout << "ItemTable: loaded " << byId.size() << " items\n";
	return !byId.empty();
}

size_t ItemTable::Size() const {
	return byId.size();
}

const string* ItemTable::BaseName(const string& itemTypeId) const {
	auto it = byId.find(itemTypeId);
	if (it != byId.end()) {
		return &it->second;
	}

	// ids arrive as T4_X@2; the table only stores the base form
	size_t at = itemTypeId.find('@');
	if (at != string::npos) {
		it = byId.find(itemTypeId.substr(0, at));
		if (it != byId.end()) {
			return &it->second;
		}
	}
	return NULL;
}

string ItemTable::SearchString(const string& itemTypeId, int tier,
	int enchant) const {

	const string* name = BaseName(itemTypeId);
	if (name == NULL) {
		return "";
	}

	if (tier <= 0) {
		tier = TierFromId(itemTypeId);
	}

	// the game parses "Boltcasters T4.2" on its own
	string out;
	out.reserve(name->size() + 8);
	out += *name;
	out += " T";
	out += to_string(tier);
	out += '.';
	out += to_string(enchant);
	return out;
}


// --------------------------------------------------------------- MarketFeed

MarketFeed::MarketFeed() {
	buffer.reserve(512);
}

MarketFeed::~MarketFeed() {
	Stop();
}

bool MarketFeed::IsRunning() const {
	return running.load(memory_order_relaxed);
}

unsigned long long MarketFeed::MessagesSeen() const {
	return messagesSeen.load(memory_order_relaxed);
}

unsigned long long MarketFeed::OrdersSeen() const {
	return ordersSeen.load(memory_order_relaxed);
}

int MarketFeed::LastOrderEnchant() const {
	return lastOrderEnchant.load(memory_order_relaxed);
}

bool MarketFeed::Start(unsigned short port) {
	if (running.load()) {
		return true;
	}

	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
		cout << "MarketFeed: WSAStartup failed\n";
		return false;
	}

	SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s == INVALID_SOCKET) {
		cout << "MarketFeed: socket() failed\n";
		WSACleanup();
		return false;
	}

	// so a restart right after a crash is not refused by a lingering socket
	BOOL reuse = TRUE;
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

	sockaddr_in addr;
	ZeroMemory(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	// loopback only: this must never be reachable from outside the machine
	inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

	if (::bind(s, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
		cout << "MarketFeed: cannot bind port " << port
			<< " (already in use?)\n";
		closesocket(s);
		WSACleanup();
		return false;
	}

	if (listen(s, SOMAXCONN) == SOCKET_ERROR) {
		cout << "MarketFeed: listen() failed\n";
		closesocket(s);
		WSACleanup();
		return false;
	}
	u_long nonblocking = 1;
	if (ioctlsocket(s, FIONBIO, &nonblocking) == SOCKET_ERROR) {
		closesocket(s); WSACleanup(); return false;
	}

	listenSocket.store((uintptr_t)s);
	int addressLength = sizeof(addr);
	if (getsockname(s, (sockaddr*)&addr, &addressLength) == 0) boundPort.store(ntohs(addr.sin_port));
	running.store(true);
	worker = thread(&MarketFeed::ListenLoop, this);

	cout << "MarketFeed: listening on 127.0.0.1:" << boundPort.load() << '\n';
	return true;
}

void MarketFeed::Stop() {
	if (!running.exchange(false)) {
		return;
	}

	// The listener is nonblocking and polled with a bounded wait. Its owning
	// thread finishes before the socket is closed, avoiding cross-thread close
	// races in accept/select.
	SOCKET s = (SOCKET)listenSocket.exchange(0);
	{
		lock_guard<mutex> guard(clientLock);
		if (activeClient) {
			// Winsock shutdown does not reliably cancel a blocked recv. Closing
			// does; the shared ownership flag prevents the reader closing twice.
			closesocket((SOCKET)activeClient);
			activeClient = 0;
		}
	}

	if (worker.joinable()) {
		worker.join();
	}
	if (s != 0) closesocket(s);
	WSACleanup();
	boundPort.store(0);
}

void MarketFeed::Drain(vector<FeedOrder>& out) {
	out.clear();
	lock_guard<mutex> guard(bufferLock);
	buffer.swap(out);
}

void MarketFeed::DrainPurchases(vector<FeedPurchase>& out) {
	out.clear();
	lock_guard<mutex> guard(purchaseLock);
	purchases.swap(out);
}

unsigned long long MarketFeed::PurchasesSeen() const {
	return purchasesSeen.load(memory_order_relaxed);
}

void MarketFeed::ListenLoop() {
	while (running.load()) {
		SOCKET listener = (SOCKET)listenSocket.load();
		if (listener == 0) {
			break;
		}
		fd_set readable; FD_ZERO(&readable); FD_SET(listener, &readable);
		timeval timeout{0, 100000};
		const int ready = select(0, &readable, nullptr, nullptr, &timeout);
		if (!running.load()) break;
		if (ready <= 0) continue;

		SOCKET client = accept(listener, NULL, NULL);
		if (client == INVALID_SOCKET) {
			// Stop() closed the listener, or the accept genuinely failed
			if (!running.load()) {
				break;
			}
			continue;
		}

		// Serve closes the socket itself, as soon as the response is out and
		// before it parses anything - see below.
		{
			lock_guard<mutex> guard(clientLock);
			if (!running.load()) { closesocket(client); break; }
			activeClient = (uintptr_t)client;
		}
		u_long blocking = 0;
		ioctlsocket(client, FIONBIO, &blocking);
		try { Serve((uintptr_t)client); }
		catch (const std::exception&) { rejectedMessages.fetch_add(1); }
	}
}

void MarketFeed::Serve(uintptr_t clientHandle) {
	SOCKET client = (SOCKET)clientHandle;

	// Don't let a half-open connection wedge the thread. This is the whole
	// accept loop's only thread, so every second spent here is a second in
	// which AlbionFeed's next batch sits unread in the backlog: five seconds
	// was long enough to stall a scan outright. The sender is on loopback and
	// has already written the request before connecting, so anything that has
	// not arrived within a second is not coming.
	DWORD timeout = 1000;
	setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout,
		sizeof(timeout));

	// Send the small 200 OK the moment it is ready instead of letting Nagle
	// hold it back waiting for more to coalesce. Combined with the delayed ACK
	// at the other end that stall is tens of milliseconds, paid on every
	// single batch, for a response that is never followed by more data.
	BOOL nodelay = TRUE;
	setsockopt(client, IPPROTO_TCP, TCP_NODELAY, (const char*)&nodelay,
		sizeof(nodelay));

	// Closed on every path out of here, including the early returns below.
	// Release() hands ownership back so the socket can be shut down early,
	// which is the point of the whole arrangement - see the send() below.
	struct SocketCloser {
		SOCKET handle;
		mutex& clientLock;
		uintptr_t& activeClient;
		~SocketCloser() { Close(); }
		void Close() {
			lock_guard<mutex> guard(clientLock);
			if (handle != INVALID_SOCKET) {
				if (activeClient == (uintptr_t)handle) {
					activeClient = 0;
					closesocket(handle);
				}
				handle = INVALID_SOCKET;
			}
		}
	} closer{ client, clientLock, activeClient };

	string request;
	request.reserve(16384);

	char chunk[8192];
	size_t headerEnd = string::npos;

	// read until the end of the headers
	while (true) {
		int got = recv(client, chunk, sizeof(chunk), 0);
		if (got <= 0) {
			return;
		}
		request.append(chunk, got);

		headerEnd = request.find("\r\n\r\n");
		if (headerEnd != string::npos) {
			break;
		}
		if (request.size() > (64u << 10)) {
			rejectedMessages.fetch_add(1);
			return;
		}
	}

	// Content-Length tells us how much body to expect
	size_t contentLength = 0;
	{
		static const char* kKey = "content-length:";
		string lower;
		lower.resize(headerEnd);
		for (size_t i = 0; i < headerEnd; ++i) {
			lower[i] = (char)tolower((unsigned char)request[i]);
		}

		size_t at = lower.find(kKey);
		if (at != string::npos) {
			contentLength = (size_t)strtoull(
				request.c_str() + at + strlen(kKey), NULL, 10);
		}
	}

	size_t bodyStart = headerEnd + 4;
	if (contentLength == 0 || contentLength > (8u << 20)) {
		rejectedMessages.fetch_add(1); return;
	}
	while (request.size() - bodyStart < contentLength) {
		int got = recv(client, chunk, sizeof(chunk), 0);
		if (got <= 0) {
			rejectedMessages.fetch_add(1); return;
		}
		request.append(chunk, got);
	}

	// Answer before parsing: the client treats anything but 200 as an error,
	// and there is no reason to make it wait on our JSON work.
	static const char kOk[] =
		"HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
	send(client, kOk, (int)(sizeof(kOk) - 1), 0);

	// And hang up before parsing too, not just answer. The connection used to
	// stay open for as long as Ingest took, which on a full listing is a
	// non-trivial amount of JSON - AlbionFeed announces Connection: close and
	// waits for the socket to go down before sending the next batch, so every
	// batch was paying for the parse of the one before it. A graceful close
	// still flushes the response that was just queued.
	closer.Close();

	size_t available = request.size() - bodyStart;
	if (available > 0) {
		Ingest(request.c_str() + bodyStart,
			available < contentLength ? available : contentLength);
	}
}

void MarketFeed::Ingest(const char* body, size_t length) {
	if (length == 0) {
		return;
	}

	json doc = json::parse(body, body + length, nullptr, false);
	if (doc.is_discarded() || !doc.is_object()) {
		rejectedMessages.fetch_add(1);
		return;
	}

	// A purchase the game reported. Handled before orders because the two
	// bodies are disjoint, and this one is small.
	auto purchaseIt = doc.find("Purchase");
	if (purchaseIt != doc.end() && !purchaseIt->is_object()) { rejectedMessages.fetch_add(1); return; }
	if (purchaseIt != doc.end() && purchaseIt->is_object()) {
		FeedPurchase bought;

		auto orderIdIt = purchaseIt->find("OrderId");
		if (orderIdIt != purchaseIt->end() && orderIdIt->is_number()) {
			bought.orderId = orderIdIt->get<long long>();
		}
		auto amountIt = purchaseIt->find("Amount");
		if (amountIt != purchaseIt->end() && amountIt->is_number()) {
			bought.amount = amountIt->get<int>();
		}
		auto confirmedIt = purchaseIt->find("Confirmed");
		if (confirmedIt != purchaseIt->end() && confirmedIt->is_boolean()) {
			bought.confirmed = confirmedIt->get<bool>();
		}

		if (bought.orderId <= 0 || bought.amount <= 0 || confirmedIt == purchaseIt->end() || !confirmedIt->is_boolean()) {
			rejectedMessages.fetch_add(1); return;
		}
		if (bought.orderId != 0) {
			{
				lock_guard<mutex> guard(purchaseLock);
				purchases.push_back(bought);
			}
			purchasesSeen.fetch_add(1, memory_order_release);
		}

		// Deliberately not counted as a message. MessagesSeen is what the
		// scanner and the buyer poll to mean "fresh market data arrived", and a
		// purchase carries no prices - counting it would end those waits early
		// with nothing new to read.
		return;
	}

	auto ordersIt = doc.find("Orders");
	if (ordersIt == doc.end() || !ordersIt->is_array()) {
		// Unrelated topics cannot satisfy a scanner's wait for market data.
		if (ordersIt != doc.end()) rejectedMessages.fetch_add(1);
		return;
	}

	vector<FeedOrder> parsed;
	lastMarketTick.store(GetTickCount64());
	parsed.reserve(ordersIt->size());

	for (const auto& node : *ordersIt) {
		try {
		if (!node.is_object()) { rejectedMessages.fetch_add(1); continue; }
		FeedOrder order;

		auto get = [&node](const char* key) -> const json* {
			auto it = node.find(key);
			return it == node.end() || it->is_null() ? NULL : &(*it);
		};

		if (const json* v = get("Id")) {
			order.id = v->get<long long>();
		}
		if (const json* v = get("ItemTypeId")) {
			order.itemTypeId = v->get<string>();
		}
		if (order.itemTypeId.empty()) {
			continue;
		}

		if (const json* v = get("ItemGroupTypeId")) {
			order.groupTypeId = v->get<string>();
		}
		if (order.groupTypeId.empty()) {
			order.groupTypeId = StripEnchantSuffix(order.itemTypeId);
		}

		if (const json* v = get("LocationId")) {
			// normally a string like "3003", but be tolerant of a number
			order.locationId = v->is_string()
				? v->get<string>()
				: to_string(v->get<long long>());
		}
		if (const json* v = get("QualityLevel")) {
			order.quality = v->get<int>();
		}
		if (const json* v = get("EnchantmentLevel")) {
			order.enchant = v->get<int>();
		}
		if (const json* v = get("Amount")) {
			order.amount = v->get<int>();
		}
		if (const json* v = get("UnitPriceSilver")) {
			order.price = v->get<long long>() / PRICE_SCALE;
		}
		if (const json* v = get("AuctionType")) {
			// "request" = someone wants to buy, "offer" = someone is selling
			const string type = v->get<string>();
			if (type != "request" && type != "offer") { rejectedMessages.fetch_add(1); continue; }
			order.isBuyOrder = type == "request";
		}
		else { rejectedMessages.fetch_add(1); continue; }

		order.tier = TierFromId(order.itemTypeId);
		order.seenAt = static_cast<long long>(std::time(nullptr));
		if (const json* v = get("Expires")) {
			if (v->is_string()) {
				std::tm utc{};
				std::istringstream date(v->get<string>());
				date >> std::get_time(&utc, "%Y-%m-%dT%H:%M:%S");
				if (!date.fail()) order.expiresAt = _mkgmtime64(&utc);
			}
		}

		if (order.id <= 0 || order.price <= 0 || order.amount <= 0 || order.locationId.empty() ||
			order.quality < 1 || order.quality > 5 || order.enchant < 0 || order.enchant > 4) {
			rejectedMessages.fetch_add(1); continue;
		}
		parsed.push_back(std::move(order));
		} catch (const json::exception&) { rejectedMessages.fetch_add(1); }
	}

	if (parsed.empty()) {
		messagesSeen.fetch_add(1, memory_order_relaxed);
		return;
	}

	size_t count = parsed.size();

	// Read before the buffer swap moves `parsed` out from under us.
	const int lastEnchant = parsed.back().enchant;

	{
		lock_guard<mutex> guard(bufferLock);
		if (buffer.empty()) {
			buffer.swap(parsed);
		}
		else {
			buffer.insert(buffer.end(),
				make_move_iterator(parsed.begin()),
				make_move_iterator(parsed.end()));
		}
	}

	lastOrderEnchant.store(lastEnchant, memory_order_relaxed);

	// published after the data is visible, so a reader that notices the
	// counter move will find the orders already in the buffer
	ordersSeen.fetch_add(count, memory_order_release);
	messagesSeen.fetch_add(1, memory_order_release);
}
