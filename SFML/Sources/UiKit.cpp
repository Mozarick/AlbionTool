#include "../Headers/UiKit.h"

#include <cmath>
#include <iostream>

namespace {

	const float PI = 3.14159265358979f;

	// SFML 2.5 ships no rounded rectangle, so here is one: a Shape whose point
	// list walks four quarter-circles. Everything with a corner radius in this
	// UI goes through it.
	class RoundedRect : public sf::Shape {
	public:
		RoundedRect(const sf::Vector2f& size, float radius, unsigned corner = 8)
			: size(size), radius(radius), corner(corner < 2 ? 2 : corner) {

			// A radius past half the shorter side folds the outline inside
			// out. Clamping is what lets callers pass RadiusPill (22) to a
			// short control without checking first.
			const float limit = (size.x < size.y ? size.x : size.y) * 0.5f;
			if (this->radius > limit) {
				this->radius = limit;
			}
			if (this->radius < 0.f) {
				this->radius = 0.f;
			}
			update();
		}

		std::size_t getPointCount() const override {
			return corner * 4;
		}

		sf::Vector2f getPoint(std::size_t index) const override {
			if (index >= corner * 4) {
				return sf::Vector2f(0.f, 0.f);
			}

			const float delta = 90.f / float(corner - 1);
			const unsigned centreIndex = unsigned(index) / corner;

			sf::Vector2f centre;
			switch (centreIndex) {
			case 0: centre = sf::Vector2f(size.x - radius, radius); break;
			case 1: centre = sf::Vector2f(radius, radius); break;
			case 2: centre = sf::Vector2f(radius, size.y - radius); break;
			default: centre = sf::Vector2f(size.x - radius, size.y - radius); break;
			}

			const float angle = delta * float(index - centreIndex) * PI / 180.f;
			return sf::Vector2f(radius * std::cos(angle) + centre.x,
				-radius * std::sin(angle) + centre.y);
		}

	private:
		sf::Vector2f size;
		float radius;
		unsigned corner;
	};

	// The faces. Static because sf::Text keeps a pointer to its font rather
	// than a copy, so a font that goes out of scope draws nothing at all -
	// silently, which is the worst way for this to fail.
	sf::Font fontRegular, fontMedium, fontSemibold, fontBold, fontMono;
	bool fontsLoaded = false;

	struct Spec {
		const sf::Font* face;
		unsigned size;
		float line;
	};

	Spec SpecFor(Ui::Text style) {
		switch (style) {
		case Ui::Text::LargeTitle:     return { &fontBold,     34, 41.f };
		case Ui::Text::Title2:         return { &fontBold,     22, 28.f };
		case Ui::Text::Headline:       return { &fontSemibold, 17, 22.f };
		case Ui::Text::Body:           return { &fontRegular,  17, 22.f };
		case Ui::Text::Callout:        return { &fontRegular,  16, 21.f };
		case Ui::Text::CalloutStrong:  return { &fontSemibold, 16, 21.f };
		case Ui::Text::Subhead:        return { &fontRegular,  15, 20.f };
		case Ui::Text::Footnote:       return { &fontRegular,  13, 18.f };
		case Ui::Text::FootnoteStrong: return { &fontSemibold, 13, 18.f };
		case Ui::Text::Caption:        return { &fontRegular,  12, 16.f };
		default:                       return { &fontMono,     11, 15.f };
		}
	}

	// Vertical centring. SFML positions text by the top of its line box, not
	// its baseline, so a row centred by halving the glyph bounds drifts with
	// every different string. Centring the LINE instead keeps every row's text
	// on the same baseline whatever it says.
	float TextTopFor(float boxTop, float boxHeight, Ui::Text style) {
		const Spec s = SpecFor(style);
		return boxTop + (boxHeight - s.line) * 0.5f;
	}
}


namespace Ui {

	bool LoadFonts(const std::string& dir) {
		if (fontsLoaded) {
			return true;
		}

		struct Item { sf::Font* font; const char* file; };
		const Item items[] = {
			{ &fontRegular,  "Inter-Regular.ttf" },
			{ &fontMedium,   "Inter-Medium.ttf" },
			{ &fontSemibold, "Inter-SemiBold.ttf" },
			{ &fontBold,     "Inter-Bold.ttf" },
			{ &fontMono,     "CascadiaCode.ttf" }
		};

		bool all = true;
		for (const Item& item : items) {
			if (!item.font->loadFromFile(dir + item.file)) {
				std::cout << "UiKit: cannot load " << dir << item.file << '\n';
				all = false;
			}
		}

		// A missing weight must not blank the screen. Point whatever failed at
		// the mono face, which is the one this project has always shipped.
		if (!all) {
			std::cout << "UiKit: falling back to the mono face for missing weights.\n";
			if (fontRegular.getInfo().family.empty())  fontRegular = fontMono;
			if (fontMedium.getInfo().family.empty())   fontMedium = fontMono;
			if (fontSemibold.getInfo().family.empty()) fontSemibold = fontMono;
			if (fontBold.getInfo().family.empty())     fontBold = fontMono;
		}

		fontsLoaded = true;
		return all;
	}

	sf::Text Make(const std::string& s, Text style, const sf::Color& colour) {
		const Spec spec = SpecFor(style);
		sf::Text t;
		t.setFont(*spec.face);
		t.setCharacterSize(spec.size);
		t.setFillColor(colour);
		t.setString(sf::String::fromUtf8(s.begin(), s.end()));
		return t;
	}

	void Draw(sf::RenderTarget& target, const std::string& s, Text style,
		const sf::Color& colour, float x, float y) {

		sf::Text t = Make(s, style, colour);
		t.setPosition(std::floor(x), std::floor(y));
		target.draw(t);
	}

	void DrawRight(sf::RenderTarget& target, const std::string& s, Text style,
		const sf::Color& colour, float rightX, float y) {

		sf::Text t = Make(s, style, colour);
		const sf::FloatRect b = t.getLocalBounds();
		t.setPosition(std::floor(rightX - (b.left + b.width)), std::floor(y));
		target.draw(t);
	}

	float Width(const std::string& s, Text style) {
		sf::Text t = Make(s, style, sf::Color::White);
		const sf::FloatRect b = t.getLocalBounds();
		return b.left + b.width;
	}

	float LineHeight(Text style) {
		return SpecFor(style).line;
	}

	// ------------------------------------------------------------- shapes
	void FillRounded(sf::RenderTarget& target, const sf::FloatRect& r,
		float radius, const sf::Color& fill) {

		RoundedRect shape(sf::Vector2f(r.width, r.height), radius);
		shape.setPosition(r.left, r.top);
		shape.setFillColor(fill);
		target.draw(shape);
	}

	void StrokeRounded(sf::RenderTarget& target, const sf::FloatRect& r,
		float radius, const sf::Color& stroke, float weight) {

		// Inset by half the stroke so the outline lands inside the rect the
		// caller asked for rather than straddling its edge.
		const float h = weight * 0.5f;
		RoundedRect shape(sf::Vector2f(r.width - weight, r.height - weight),
			radius - h);
		shape.setPosition(r.left + h, r.top + h);
		shape.setFillColor(sf::Color::Transparent);
		shape.setOutlineColor(stroke);
		shape.setOutlineThickness(weight);
		target.draw(shape);
	}

	void DrawSeparator(sf::RenderTarget& target, float x, float y, float w) {
		sf::RectangleShape line(sf::Vector2f(w, 1.f));
		line.setPosition(std::floor(x), std::floor(y));
		line.setFillColor(Theme::Border);
		target.draw(line);
	}

	// -------------------------------------------------------------- lists
	GroupGeom LayoutGroup(float x, float y, float w, bool hasHeader,
		size_t rowCount) {

		GroupGeom g;
		g.header = hasHeader ? sf::FloatRect(x, y, w, HeaderH)
			: sf::FloatRect(x, y, w, 0.f);

		const float top = y + (hasHeader ? HeaderH : 0.f);
		g.box = sf::FloatRect(x, top, w, RowH * float(rowCount));

		g.rows.reserve(rowCount);
		for (size_t i = 0; i < rowCount; ++i) {
			g.rows.push_back(sf::FloatRect(x, top + RowH * float(i), w, RowH));
		}
		g.bottom = top + RowH * float(rowCount);
		return g;
	}

	void DrawGroup(sf::RenderTarget& target, const GroupGeom& g,
		const std::string& header, const std::vector<Row>& rows,
		const std::string& hotCommand) {

		if (g.header.height > 0.f && !header.empty()) {
			Draw(target, header, Text::FootnoteStrong, Theme::TextMuted,
				g.header.left + RowInset,
				g.header.top + g.header.height - LineHeight(Text::FootnoteStrong) - 4.f);
		}

		FillRounded(target, g.box, RadiusGroup, Theme::Panel);

		const size_t n = rows.size() < g.rows.size() ? rows.size() : g.rows.size();
		for (size_t i = 0; i < n; ++i) {
			const sf::FloatRect& r = g.rows[i];
			const Row& row = rows[i];

			// The pressed fill has to respect the group's rounded ends, so the
			// first and last rows get the group radius and the middle ones get
			// square corners.
			if (!row.command.empty() && row.command == hotCommand) {
				const bool end = (i == 0) || (i + 1 == n);
				FillRounded(target, r, end ? RadiusGroup : 0.f, Theme::PanelRaised);
			}

			if (i > 0) {
				DrawSeparator(target, r.left + RowInset, r.top,
					r.width - RowInset);
			}

			sf::Color label = Theme::TextPrimary;
			if (row.tone == Tone::Destructive) label = Theme::Destructive;
			else if (row.tone == Tone::Accent)  label = Theme::Accent;

			Draw(target, row.label, Text::Body, label,
				r.left + RowInset, TextTopFor(r.top, r.height, Text::Body));

			float rightEdge = r.left + r.width - RowInset;
			if (row.chevron) {
				Draw(target, "\xE2\x80\xBA", Text::Headline, Theme::TextMuted,
					rightEdge - 6.f,
					TextTopFor(r.top, r.height, Text::Headline));
				rightEdge -= 18.f;
			}
			if (!row.value.empty()) {
				DrawRight(target, row.value, Text::Body, Theme::TextSecondary,
					rightEdge, TextTopFor(r.top, r.height, Text::Body));
			}
		}
	}

	std::string HitGroup(const GroupGeom& g, const std::vector<Row>& rows,
		float px, float py) {

		const size_t n = rows.size() < g.rows.size() ? rows.size() : g.rows.size();
		for (size_t i = 0; i < n; ++i) {
			if (g.rows[i].contains(px, py)) {
				return rows[i].command;
			}
		}
		return "";
	}

	// ------------------------------------------------------------ buttons
	void DrawButton(sf::RenderTarget& target, const sf::FloatRect& r,
		const std::string& label, ButtonStyle style, bool pressed) {

		sf::Color fill = Theme::Accent;
		sf::Color text = Theme::TextPrimary;
		switch (style) {
		case ButtonStyle::Tinted:      fill = Theme::AccentTint; text = Theme::AccentText; break;
		case ButtonStyle::Destructive: fill = Theme::Destructive; break;
		default: break;
		}

		// Pressed dims rather than moves. A run blocks the event thread, so
		// nothing here can animate anyway - and a colour multiply is exactly
		// what the Figma "Pressed" variant is.
		if (pressed) {
			fill = sf::Color(sf::Uint8(fill.r * 0.6f), sf::Uint8(fill.g * 0.6f),
				sf::Uint8(fill.b * 0.6f), fill.a);
		}

		FillRounded(target, r, RadiusPill, fill);
		const float w = Width(label, Text::Headline);
		Draw(target, label, Text::Headline, text,
			r.left + (r.width - w) * 0.5f,
			TextTopFor(r.top, r.height, Text::Headline));
	}

	// -------------------------------------------------------------- chips
	void DrawChip(sf::RenderTarget& target, const sf::FloatRect& r,
		const std::string& label, ChipState state) {

		sf::Color fill = Theme::Panel;
		sf::Color line = sf::Color::Transparent;
		sf::Color text = Theme::TextSecondary;
		Text style = Text::FootnoteStrong;

		switch (state) {
		case ChipState::NotInRun:
			text = Theme::TextMuted; style = Text::Footnote; break;
		case ChipState::Pending:
			break;
		case ChipState::Running:
			fill = Theme::RunningTint; line = Theme::Running; text = Theme::Running; break;
		case ChipState::Ok:
			fill = Theme::OkTint; line = Theme::Ok; text = Theme::Ok; break;
		case ChipState::Failed:
			fill = Theme::FailedTint; line = Theme::Failed; text = Theme::Failed; break;
		case ChipState::Selected:
			fill = Theme::AccentTint; line = Theme::Accent; text = Theme::AccentText; break;
		}

		FillRounded(target, r, RadiusChip, fill);
		if (line.a > 0) {
			StrokeRounded(target, r, RadiusChip, line, 1.f);
		}
		Draw(target, label, style, text,
			r.left + 12.f, TextTopFor(r.top, r.height, style));
	}

	sf::FloatRect ChipRect(size_t index, float x0, float y0) {
		const size_t col = index % 4;
		const size_t row = index / 4;
		return sf::FloatRect(x0 + float(col) * (ChipW + ChipGapX),
			y0 + float(row) * ChipPitchY, ChipW, ChipH);
	}

	float GridBottom(size_t count, float top) {
		return top + float((count + 3) / 4) * ChipPitchY;
	}

	namespace {
		const char* const CATEGORY_PREFIX = "cat:";
		const size_t CATEGORY_PREFIX_LEN = 4;
	}

	std::string CategoryCommand(const std::string& key) {
		return std::string(CATEGORY_PREFIX) + key;
	}

	std::string CategoryKey(const std::string& command) {
		if (command.size() <= CATEGORY_PREFIX_LEN ||
			command.compare(0, CATEGORY_PREFIX_LEN, CATEGORY_PREFIX) != 0) {
			return "";
		}
		return command.substr(CATEGORY_PREFIX_LEN);
	}

	// ---------------------------------------------------------- segmented
	void DrawSegmented(sf::RenderTarget& target, const sf::FloatRect& r,
		const std::vector<std::string>& labels, size_t selected) {

		if (labels.empty()) {
			return;
		}
		FillRounded(target, r, 9.f, Theme::PanelRaised);

		const float pad = 2.f;
		const float segW = (r.width - pad * 2.f) / float(labels.size());
		for (size_t i = 0; i < labels.size(); ++i) {
			const sf::FloatRect seg(r.left + pad + segW * float(i), r.top + pad,
				segW, r.height - pad * 2.f);
			const bool on = (i == selected);
			if (on) {
				FillRounded(target, seg, 7.f, Theme::BorderStrong);
			}
			const Text style = on ? Text::FootnoteStrong : Text::Footnote;
			const float w = Width(labels[i], style);
			Draw(target, labels[i], style,
				on ? Theme::TextPrimary : Theme::TextSecondary,
				seg.left + (seg.width - w) * 0.5f,
				TextTopFor(seg.top, seg.height, style));
		}
	}

	int HitSegmented(const sf::FloatRect& r, size_t count, float px, float py) {
		if (count == 0 || !r.contains(px, py)) {
			return -1;
		}
		const float pad = 2.f;
		const float segW = (r.width - pad * 2.f) / float(count);
		const int idx = int((px - r.left - pad) / segW);
		if (idx < 0) return 0;
		if (idx >= int(count)) return int(count) - 1;
		return idx;
	}

	// ----------------------------------------------------------- progress
	void DrawProgress(sf::RenderTarget& target, const sf::FloatRect& r,
		float fraction) {

		if (fraction < 0.f) fraction = 0.f;
		if (fraction > 1.f) fraction = 1.f;

		const float radius = r.height * 0.5f;
		FillRounded(target, r, radius, Theme::PanelRaised);
		if (fraction > 0.f) {
			const sf::FloatRect fill(r.left, r.top, r.width * fraction, r.height);
			FillRounded(target, fill, radius, Theme::Accent);
		}
	}

	// ------------------------------------------------------------- navbar
	sf::FloatRect NavBackRect() {
		return sf::FloatRect(8.f, 0.f, 110.f, NavH);
	}

	void DrawNavBar(sf::RenderTarget& target, float width,
		const std::string& title, const std::string& backLabel,
		bool backPressed) {

		if (!backLabel.empty()) {
			const sf::Color tint = backPressed
				? sf::Color(sf::Uint8(Theme::Back.r * 0.6f),
					sf::Uint8(Theme::Back.g * 0.6f),
					sf::Uint8(Theme::Back.b * 0.6f))
				: Theme::Back;

			Draw(target, "\xE2\x80\xB9", Text::Title2, tint,
				12.f, TextTopFor(0.f, NavH, Text::Title2));
			Draw(target, backLabel, Text::Body, tint,
				28.f, TextTopFor(0.f, NavH, Text::Body));
		}

		if (!title.empty()) {
			const float w = Width(title, Text::Headline);
			Draw(target, title, Text::Headline, Theme::TextPrimary,
				(width - w) * 0.5f, TextTopFor(0.f, NavH, Text::Headline));
		}
	}

	// --------------------------------------------------------------- misc
	void DrawPill(sf::RenderTarget& target, const sf::FloatRect& r,
		const std::string& label, bool on) {

		FillRounded(target, r, RadiusChip, on ? Theme::OkTint : Theme::Panel);
		if (on) {
			StrokeRounded(target, r, RadiusChip, Theme::Ok, 1.f);
		}
		const Text style = on ? Text::FootnoteStrong : Text::Footnote;
		const float w = Width(label, style);
		Draw(target, label, style, on ? Theme::Ok : Theme::TextSecondary,
			r.left + (r.width - w) * 0.5f,
			TextTopFor(r.top, r.height, style));
	}
}
