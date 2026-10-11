using Albion.Network;
using PacketDotNet;
using PhotonPackageParser;
using SharpPcap;

namespace AlbionFeed;

/// <summary>
/// Reads Albion's UDP traffic off the wire with Npcap and feeds it to the
/// Photon parser.
///
/// Npcap is a passive capture driver: it copies packets on their way past and
/// does not modify, block or reroute anything.
/// </summary>
public sealed class Capture : IDisposable
{
    private readonly IPhotonReceiver receiver;
    private readonly string filter;
    private readonly List<ILiveDevice> opened = new();
    private readonly object parseLock = new();

    // ICaptureDevice, not ILiveDevice: that is what a packet reports itself as
    // arriving on, and the two are compared by reference here.
    private ICaptureDevice? chosen;
    private long packets;
    private bool warnedEncrypted;

    public Capture(IPhotonReceiver receiver, string filter)
    {
        this.receiver = receiver;
        this.filter = filter;
    }

    public long Packets => Interlocked.Read(ref packets);

    /// <summary>
    /// Opens every device, because there is no reliable way to know in advance
    /// which adapter the game's traffic uses - VPNs, virtual switches and
    /// several NICs all look plausible from here. The first one that actually
    /// delivers an Albion packet wins and the rest are closed, so the parser
    /// only ever sees one copy of the stream (it reassembles fragments, and two
    /// copies of the same packet would corrupt that).
    /// </summary>
    public int Start()
    {
        var devices = CaptureDeviceList.Instance;
        if (devices.Count == 0)
        {
            Log.Error("no capture devices - is Npcap installed, and is this running as Administrator?");
            return 0;
        }

        foreach (var device in devices)
        {
            try
            {
                device.OnPacketArrival += OnPacket;
                device.Open(new DeviceConfiguration
                {
                    Mode = DeviceModes.None,
                    ReadTimeout = 5000,
                });
                device.Filter = filter;
                device.StartCapture();
                opened.Add(device);
            }
            catch (Exception ex)
            {
                device.OnPacketArrival -= OnPacket;
                Log.Warn($"skipping device {Describe(device)}: {ex.Message}");
            }
        }

        Log.Info($"listening on {opened.Count} network device(s) with filter \"{filter}\"");
        return opened.Count;
    }

    private static string Describe(ICaptureDevice device)
    {
        return string.IsNullOrWhiteSpace(device.Description) ? device.Name : device.Description;
    }

    private void OnPacket(object sender, PacketCapture e)
    {
        try
        {
            var device = e.Device;
            if (chosen != null && !ReferenceEquals(chosen, device))
            {
                return;
            }

            var raw = e.GetPacket();
            var udp = Packet.ParsePacket(raw.LinkLayerType, raw.Data)?.Extract<UdpPacket>();
            if (udp == null)
            {
                return;
            }

            if (chosen == null)
            {
                chosen = device;
                Log.Info($"using device {Describe(device)}");
                CloseOthers(device);
            }

            Interlocked.Increment(ref packets);

            // The parser keeps reassembly state across calls, so one packet at
            // a time. Devices deliver on their own threads.
            PacketStatus status;
            lock (parseLock)
            {
                status = receiver.ReceivePacket(udp.PayloadData);
            }

            if (status == PacketStatus.Encrypted && !warnedEncrypted)
            {
                warnedEncrypted = true;
                Log.Warn("the game is sending encrypted packets - market data cannot be read");
            }
        }
        catch (Exception ex)
        {
            Log.Error($"while processing a packet: {ex.Message}");
        }
    }

    private void CloseOthers(ICaptureDevice keep)
    {
        foreach (var device in opened)
        {
            if (ReferenceEquals(device, keep))
            {
                continue;
            }

            // Not on this thread: StopCapture blocks until the device's own
            // callback thread has finished, and we are inside one of those.
            var closing = device;
            _ = Task.Run(() => CloseQuietly(closing));
        }
    }

    private void CloseQuietly(ILiveDevice device)
    {
        try
        {
            device.OnPacketArrival -= OnPacket;
            if (device.Started)
            {
                device.StopCapture();
            }
            device.Close();
        }
        catch (Exception ex)
        {
            Log.Warn($"could not close {Describe(device)}: {ex.Message}");
        }
    }

    public void Dispose()
    {
        foreach (var device in opened)
        {
            CloseQuietly(device);
        }
        opened.Clear();
    }
}
