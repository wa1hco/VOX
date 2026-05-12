#include "dongle_client.h"

#include <QDebug>
#include <QSerialPortInfo>
#include <cstring>

/*
 * dongle_client.cpp — implementation.
 *
 * The interesting parts:
 *
 *   onMessage()        — fires per fully decoded protocol frame.
 *                        Each VoxMsgType emits the corresponding Qt
 *                        signal (with the payload unpacked into a
 *                        Qt-friendly form).  STATE_FRAME goes through
 *                        a translation from the packed wire struct
 *                        (VoxStateFrameV1) to vox_core's in-memory
 *                        VoxLedState + VoxDebugState shape, which the
 *                        GUI widgets already know how to consume.
 *
 *   onReadyRead()      — feeds all buffered serial bytes through the
 *                        protocol parser.  Slots like onMessage are
 *                        invoked synchronously from inside this slot,
 *                        but they only emit signals (no blocking work),
 *                        so we don't risk reentrancy.
 *
 *   sendInjectPcm      — the hot path: encodes one VoxInjectPcm
 *                        message (644-byte payload) and writes it
 *                        out.  At 50 fps this is ~32 KB/s, well
 *                        within 921600 baud (≈92 KB/s).
 */

DongleClient::DongleClient(QObject *parent)
    : QObject(parent)
{
    vox_proto_parser_init(&parser_);
    connect(&serial_, &QSerialPort::readyRead,
            this,     &DongleClient::onReadyRead);
    connect(&serial_, &QSerialPort::errorOccurred,
            this,     &DongleClient::onErrorOccurred);
}

DongleClient::~DongleClient()
{
    close();
}

bool DongleClient::open(const QString &portPath, int baud)
{
    if (serial_.isOpen())
        serial_.close();

    serial_.setPortName(portPath);
    serial_.setBaudRate(baud);
    serial_.setDataBits(QSerialPort::Data8);
    serial_.setParity(QSerialPort::NoParity);
    serial_.setStopBits(QSerialPort::OneStop);
    serial_.setFlowControl(QSerialPort::NoFlowControl);

    if (!serial_.open(QIODevice::ReadWrite)) {
        qWarning() << "DongleClient: cannot open" << portPath
                   << ":" << serial_.errorString();
        emit connectionChanged(false);
        return false;
    }

    /* Reset parser on every new connection so half-parsed leftovers
     * from a previous session don't poison the new stream. */
    vox_proto_parser_init(&parser_);

    /* Probe the chip — chip responds with a fresh HELLO so we learn
     * fw revision + capabilities right away rather than waiting for
     * the next chip-side boot. */
    sendQueryHello();

    emit connectionChanged(true);
    return true;
}

void DongleClient::close()
{
    if (serial_.isOpen()) {
        serial_.close();
        emit connectionChanged(false);
    }
}

/* ---- Outbound ----------------------------------------------------- */

bool DongleClient::writeFrame(VoxMsgType type, const void *payload, size_t len)
{
    if (!serial_.isOpen())
        return false;

    /* Stack-allocate a buffer for the framed message.  Largest frame
     * is INJECT_PCM at ~651 bytes; well under the typical stack
     * frame for a GUI thread. */
    std::array<uint8_t, VOX_PROTO_MAX_FRAME> buf;
    const size_t n = vox_proto_encode(buf.data(), buf.size(),
                                      type, payload, len);
    if (n == 0)
        return false;

    qint64 written = serial_.write(reinterpret_cast<const char *>(buf.data()), n);
    return written == static_cast<qint64>(n);
}

void DongleClient::sendTuning(const VoxConfig &cfg)
{
    /* VoxSetTuning shares field-by-field layout with the relevant
     * VoxConfig members.  Copy explicitly so a future VoxConfig
     * field addition doesn't accidentally end up on the wire. */
    VoxSetTuning t{};
    t.hang_ms                = cfg.hang_ms;
    t.mic_led_threshold      = cfg.mic_led_threshold;
    t.rx_led_threshold       = cfg.rx_led_threshold;
    t.vad_led_prob_threshold = cfg.vad_led_prob_threshold;
    t.aec_led_reduction_pct  = cfg.aec_led_reduction_pct;
    t.rx_guard_vad_boost     = cfg.rx_guard_vad_boost;
    t.rx_guard_snr_pct       = cfg.rx_guard_snr_pct;
    (void)writeFrame(VOX_MSG_SET_TUNING, &t, sizeof(t));
}

void DongleClient::sendSetMode(quint8 voxRunMode)
{
    VoxSetMode m{};
    m.mode = voxRunMode;
    (void)writeFrame(VOX_MSG_SET_MODE, &m, sizeof(m));
}

void DongleClient::sendInjectPcm(const int16_t *mic, const int16_t *rx, int frame_size)
{
    if (frame_size != VOX_INJECT_FRAME_SAMPLES)
        return;  /* chip-side expects exactly 160 samples */

    VoxInjectPcm pcm{};
    pcm.frame_size = static_cast<uint16_t>(frame_size);
    pcm.reserved   = 0;
    std::memcpy(pcm.mic, mic, sizeof(pcm.mic));
    std::memcpy(pcm.rx,  rx,  sizeof(pcm.rx));
    (void)writeFrame(VOX_MSG_INJECT_PCM, &pcm, sizeof(pcm));
}

void DongleClient::sendQueryHello()
{
    (void)writeFrame(VOX_MSG_QUERY_HELLO, nullptr, 0);
}

/* ---- Inbound ------------------------------------------------------ */

void DongleClient::onReadyRead()
{
    const QByteArray data = serial_.readAll();
    if (data.isEmpty())
        return;
    vox_proto_parser_feed(&parser_,
                          reinterpret_cast<const uint8_t *>(data.constData()),
                          static_cast<size_t>(data.size()),
                          &DongleClient::onMessageStatic, this);
}

void DongleClient::onErrorOccurred(QSerialPort::SerialPortError err)
{
    if (err == QSerialPort::NoError)
        return;
    qWarning() << "DongleClient: serial error" << err
               << serial_.errorString();
    /* On a non-transient error (device disappeared, etc.), close so
     * the GUI's connection state matches reality. */
    if (err == QSerialPort::ResourceError ||
        err == QSerialPort::PermissionError ||
        err == QSerialPort::DeviceNotFoundError) {
        close();
    }
}

void DongleClient::onMessageStatic(void *ctx, VoxMsgType type,
                                   const uint8_t *payload, size_t len)
{
    static_cast<DongleClient *>(ctx)->onMessage(type, payload, len);
}

void DongleClient::onMessage(VoxMsgType type, const uint8_t *payload, size_t len)
{
    switch (type) {

    case VOX_MSG_HELLO:
        if (len >= sizeof(VoxHello)) {
            VoxHello h{};
            std::memcpy(&h, payload, sizeof(h));
            /* fw_revision is a NUL-padded char[16]; fromUtf8 stops at
             * the first NUL anyway, but be explicit about the length. */
            int rev_len = 0;
            while (rev_len < (int)sizeof(h.fw_revision) && h.fw_revision[rev_len])
                rev_len++;
            const QString rev = QString::fromUtf8(h.fw_revision, rev_len);
            emit helloReceived(rev, h.capabilities, h.hw_id);
        }
        break;

    case VOX_MSG_STATE_FRAME:
        if (len >= sizeof(VoxStateFrameV1)) {
            VoxStateFrameV1 f{};
            std::memcpy(&f, payload, sizeof(f));

            /* Translate packed wire struct -> the in-memory VoxLedState
             * + VoxDebugState shape the GUI widgets consume.  Field
             * names match where they map directly. */
            VoxLedState led{};
            led.mic_led              = (f.leds & (1u << 0)) != 0;
            led.rx_led               = (f.leds & (1u << 1)) != 0;
            led.vad_led              = (f.leds & (1u << 2)) != 0;
            led.aec_led              = (f.leds & (1u << 3)) != 0;
            led.ptt_led              = (f.leds & (1u << 4)) != 0;
            led.mic_level            = f.mic_level_raw;
            led.rx_level             = f.rx_level;
            led.vad_probability_raw  = f.vad_probability_raw;
            led.vad_probability      = f.vad_probability;
            led.aec_reduction_pct    = f.aec_reduction_pct;
            led.rx_active            = (f.flags & (1u << 2)) != 0;

            VoxDebugState dbg{};
            dbg.mic_level_raw                    = f.mic_level_raw;
            dbg.mic_level_post_aec               = f.mic_level_post_aec;
            dbg.rx_level                         = f.rx_level;
            dbg.noise_floor                      = f.noise_floor;
            dbg.snr_pct                          = f.snr_pct;
            dbg.energy_margin                    = f.energy_margin;
            dbg.voice_raw                        = (f.flags & (1u << 0)) != 0;
            dbg.voice_validated                  = (f.flags & (1u << 1)) != 0;
            dbg.rx_active                        = (f.flags & (1u << 2)) != 0;
            dbg.energy_ok                        = (f.flags & (1u << 3)) != 0;
            dbg.rx_guard_applied                 = (f.flags & (1u << 4)) != 0;
            dbg.ptt_reason_voice                 = (f.flags & (1u << 5)) != 0;
            dbg.ptt_reason_hang                  = (f.flags & (1u << 6)) != 0;
            dbg.effective_vad_threshold          = f.effective_vad_threshold;
            dbg.effective_snr_threshold          = f.effective_snr_threshold;
            dbg.hang_frames                      = f.hang_frames;
            dbg.hang_frames_max                  = f.hang_frames_max;
            dbg.residual_corr_pct                = f.residual_corr_pct;
            dbg.residual_corr_dbfs_tenths        = f.residual_corr_dbfs_tenths;
            dbg.residual_corr_peak_pct           = f.residual_corr_peak_pct;
            dbg.residual_corr_peak_delay_samples = f.residual_corr_peak_delay_samples;
            /* Profile bins not transmitted in V1 of the wire format —
             * residual correlation plot will be empty in dongle mode
             * until we add a dedicated profile message. */
            dbg.residual_corr_delay_min_samples  = VOX_RESIDUAL_CORR_DELAY_MIN_SAMPLES;
            dbg.residual_corr_delay_step_samples = VOX_RESIDUAL_CORR_DELAY_STEP_SAMPLES;

            const int ptt = (f.leds & (1u << 4)) != 0;
            emit stateFrameReceived(led, dbg, ptt);
        }
        break;

    case VOX_MSG_LOG:
        emit logReceived(QString::fromUtf8(reinterpret_cast<const char *>(payload),
                                           static_cast<int>(len)));
        break;

    case VOX_MSG_ACK:
        if (len >= sizeof(VoxAck)) {
            VoxAck a{};
            std::memcpy(&a, payload, sizeof(a));
            emit ackReceived(a.in_response_to, a.status, a.info);
        }
        break;

    default:
        /* Forward-compat: unknown types silently ignored. */
        break;
    }
}
