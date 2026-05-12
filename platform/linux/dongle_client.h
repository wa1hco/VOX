#ifndef VOX_DONGLE_CLIENT_H
#define VOX_DONGLE_CLIENT_H

/*
 * dongle_client.h — host-side wrapper around the VOX dongle wire protocol.
 *
 * Speaks the protocol from `tools/vox_dongle_proto.h` over a QSerialPort.
 * Used by vox_qt's "Dongle" source-mode: receive STATE_FRAME / LOG /
 * HELLO / ACK from the chip, send SET_TUNING / INJECT_PCM / SET_MODE /
 * QUERY_HELLO back.
 *
 * Signal-based design — the main window connects to:
 *   stateFrameReceived(led, debug, ptt) → push into existing widgets
 *   helloReceived(rev, caps, hwId)       → status bar
 *   logReceived(line)                    → log panel
 *   ackReceived(...)                     → optional diagnostic
 *   connectionChanged(connected)         → enable/disable UI affordances
 *
 * No threads — everything runs in the GUI thread.  QSerialPort's
 * readyRead fires on the Qt event loop, which means parser callbacks
 * run synchronously from that and can emit signals safely.
 */

#include <QObject>
#include <QSerialPort>
#include <QString>
#include <array>
#include <cstdint>

extern "C" {
#include "vox.h"               /* VoxLedState, VoxDebugState, VoxConfig */
#include "vox_dongle_proto.h"  /* wire format + parser */
}

class DongleClient : public QObject {
    Q_OBJECT

public:
    explicit DongleClient(QObject *parent = nullptr);
    ~DongleClient() override;

    /* Open the named port at `baud`.  Returns true on success.  When
     * already open, closes and reopens.  Emits connectionChanged. */
    bool open(const QString &portPath, int baud = 921600);

    /* Close the port.  Idempotent; safe to call when not open. */
    void close();

    bool   isOpen()    const { return serial_.isOpen(); }
    QString portPath() const { return serial_.portName(); }

    /* ---- Outbound commands ----
     * Each maps to a single framed protocol message.  Safe to call
     * when the port is closed; the bytes are silently dropped (a UI
     * that calls these without a connection isn't broken — sliders
     * still move, audio still flows; nothing reaches the chip until
     * the user picks a port). */

    void sendTuning(const VoxConfig &cfg);
    void sendSetMode(quint8 voxRunMode);
    void sendInjectPcm(const int16_t *mic, const int16_t *rx, int frame_size);
    void sendQueryHello();

signals:
    void stateFrameReceived(const VoxLedState &led,
                            const VoxDebugState &debug,
                            int ptt);
    void helloReceived(const QString &fwRevision,
                       quint32 capabilities,
                       quint32 hwId);
    void logReceived(const QString &line);
    void ackReceived(quint8 inResponseTo, quint8 status, quint32 info);
    void connectionChanged(bool connected);

private slots:
    void onReadyRead();
    void onErrorOccurred(QSerialPort::SerialPortError);

private:
    /* Parser-callback trampoline: vox_proto_parser_feed wants a C
     * function pointer + opaque ctx; we route that to a member call. */
    static void onMessageStatic(void *ctx, VoxMsgType type,
                                const uint8_t *payload, size_t len);
    void onMessage(VoxMsgType type, const uint8_t *payload, size_t len);

    /* Build a wire frame and write it.  Returns true on success
     * (port open + write completed). */
    bool writeFrame(VoxMsgType type, const void *payload, size_t len);

    QSerialPort     serial_;
    VoxProtoParser  parser_{};
};

#endif /* VOX_DONGLE_CLIENT_H */
