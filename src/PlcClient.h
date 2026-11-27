#pragma once

#include <QString>
#include <QVector>
#include <cstdint>
#include <mutex>
#include <atomic>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#endif

struct AddrWordWrite {
    int addr = 0;
    uint16_t word = 0;
};

class PlcClient {
public:
    PlcClient();
    ~PlcClient();

    bool connectPlc(const QString& protocol, const QString& host, int port,
                    int unitId = 1, int baudrate = 9600, int dbNum = 1,
                    int rack = 0, int slot = 1, QString* errOut = nullptr);
    void disconnectPlc();
    bool reconnect(QString* errOut = nullptr);
    bool isConnected() const { return m_connected.load(); }
    QString protocol() const;

    bool readRegisters(int addr, int count, QVector<int16_t>& outValues, QString* errOut = nullptr);
    bool writeRegisters(int addr, const QVector<uint16_t>& words, QString* errOut = nullptr);

    // 支持每个数据项独立自由定义PLC目标地址：自动合并相邻连续地址，非连续地址逐段精准写入
    bool writeMappedWords(const QVector<AddrWordWrite>& items, QString* errOut = nullptr);
    bool sendRawText(const QString& text, QString* replyOut = nullptr, QString* errOut = nullptr);

    static QVector<uint16_t> encodeValueToWords(double value, const QString& dtype, const QString& wordOrder = "ABCD");
    static int dtypeWordCount(const QString& dtype);

    uint64_t txCount() const { return m_txCount.load(); }
    uint64_t rxCount() const { return m_rxCount.load(); }

private:
    bool connectUnlocked(QString* errOut);
    void disconnectUnlocked();
    bool recvExact(void* sockPtr, uint8_t* buf, int len);
    bool sendAll(void* sockPtr, const uint8_t* buf, int len);
    static uint16_t calcCrc16(const uint8_t* data, size_t len);

    mutable std::mutex m_mutex;
    std::atomic<bool> m_connected{false};
    std::atomic<uint64_t> m_txCount{0};
    std::atomic<uint64_t> m_rxCount{0};

    QString m_protocol = "Modbus TCP";
    QString m_host = "192.168.1.10";
    int m_port = 502;
    int m_unitId = 1;
    int m_baudrate = 9600;
    int m_dbNum = 1;
    int m_rack = 0;
    int m_slot = 1;
    uint16_t m_tid = 0;

#ifdef _WIN32
    SOCKET m_sock = INVALID_SOCKET;
    HANDLE m_serial = INVALID_HANDLE_VALUE;
#else
    int m_sock = -1;
#endif
};
