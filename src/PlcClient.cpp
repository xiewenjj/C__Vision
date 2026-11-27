#include "PlcClient.h"
#include <QStringList>
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>

#ifndef _WIN32
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

namespace {
#ifdef _WIN32
struct WsaInitializer {
    WsaInitializer() {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
    }
    ~WsaInitializer() { WSACleanup(); }
};
static WsaInitializer g_wsaInit;
#endif
} // namespace

PlcClient::PlcClient() = default;

PlcClient::~PlcClient() {
    disconnectPlc();
}

QString PlcClient::protocol() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_protocol;
}

uint16_t PlcClient::calcCrc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            if (crc & 0x0001)
                crc = (crc >> 1) ^ 0xA001;
            else
                crc >>= 1;
        }
    }
    return crc;
}

int PlcClient::dtypeWordCount(const QString& dtype) {
    QString u = dtype.toUpper();
    return (u == "INT32" || u == "FLOAT32") ? 2 : 1;
}

QVector<uint16_t> PlcClient::encodeValueToWords(double value, const QString& dtype, const QString& wordOrder) {
    QString u = dtype.toUpper();
    if (u == "UINT16") {
        long v = std::lround(value);
        v = std::max(0L, std::min(65535L, v));
        return { static_cast<uint16_t>(v) };
    }
    if (u == "INT32") {
        long long v = std::llround(value);
        v = std::max(-2147483648LL, std::min(2147483647LL, v));
        uint32_t u32 = static_cast<uint32_t>(static_cast<int32_t>(v));
        uint16_t w0 = static_cast<uint16_t>((u32 >> 16) & 0xFFFF);
        uint16_t w1 = static_cast<uint16_t>(u32 & 0xFFFF);
        if (wordOrder == "CDAB") return { w1, w0 };
        return { w0, w1 };
    }
    if (u == "FLOAT32") {
        float f = static_cast<float>(value);
        uint32_t u32 = 0;
        std::memcpy(&u32, &f, sizeof(uint32_t));
        uint16_t w0 = static_cast<uint16_t>((u32 >> 16) & 0xFFFF);
        uint16_t w1 = static_cast<uint16_t>(u32 & 0xFFFF);
        if (wordOrder == "CDAB") return { w1, w0 };
        return { w0, w1 };
    }
    long v = std::lround(value);
    v = std::max(-32768L, std::min(32767L, v));
    return { static_cast<uint16_t>(static_cast<int16_t>(v)) };
}

bool PlcClient::connectPlc(const QString& protocol, const QString& host, int port,
                           int unitId, int baudrate, int dbNum, int rack, int slot, QString* errOut) {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_protocol = protocol;
    m_host = host.trimmed();
    m_port = port;
    m_unitId = unitId;
    m_baudrate = baudrate;
    m_dbNum = dbNum;
    m_rack = rack;
    m_slot = slot;
    return connectUnlocked(errOut);
}

bool PlcClient::reconnect(QString* errOut) {
    std::lock_guard<std::mutex> lk(m_mutex);
    return connectUnlocked(errOut);
}

void PlcClient::disconnectPlc() {
    std::lock_guard<std::mutex> lk(m_mutex);
    disconnectUnlocked();
}

void PlcClient::disconnectUnlocked() {
    m_connected.store(false);
#ifdef _WIN32
    if (m_sock != INVALID_SOCKET) {
        closesocket(m_sock);
        m_sock = INVALID_SOCKET;
    }
    if (m_serial != INVALID_HANDLE_VALUE) {
        CloseHandle(m_serial);
        m_serial = INVALID_HANDLE_VALUE;
    }
#else
    if (m_sock >= 0) {
        close(m_sock);
        m_sock = -1;
    }
#endif
}

bool PlcClient::connectUnlocked(QString* errOut) {
    disconnectUnlocked();

    if (m_protocol == "Modbus RTU") {
#ifdef _WIN32
        QString portPath = m_host.startsWith("\\\\.\\") ? m_host : ("\\\\.\\" + m_host);
        std::wstring wpath = portPath.toStdWString();
        HANDLE h = CreateFileW(wpath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            if (errOut) *errOut = QString("无法打开串口 %1 (错误码 %2)").arg(m_host).arg(GetLastError());
            return false;
        }
        DCB dcb{};
        dcb.DCBlength = sizeof(DCB);
        if (!GetCommState(h, &dcb)) {
            CloseHandle(h);
            if (errOut) *errOut = "读取串口参数失败";
            return false;
        }
        dcb.BaudRate = static_cast<DWORD>(m_baudrate);
        dcb.ByteSize = 8;
        dcb.Parity = NOPARITY;
        dcb.StopBits = ONESTOPBIT;
        SetCommState(h, &dcb);

        COMMTIMEOUTS to{};
        to.ReadIntervalTimeout = 30;
        to.ReadTotalTimeoutConstant = 400;
        to.ReadTotalTimeoutMultiplier = 10;
        to.WriteTotalTimeoutConstant = 400;
        SetCommTimeouts(h, &to);

        m_serial = h;
        m_connected.store(true);
        return true;
#else
        if (errOut) *errOut = "非Windows平台不支持Win32串口";
        return false;
#endif
    }

    int targetPort = (m_protocol == "Siemens S7" && m_port == 502) ? 102 : m_port;
#ifdef _WIN32
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        if (errOut) *errOut = "创建 Socket 失败";
        return false;
    }
    DWORD tv = 1500;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));
    setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(targetPort));
    if (inet_pton(AF_INET, m_host.toUtf8().constData(), &addr.sin_addr) <= 0) {
        closesocket(s);
        if (errOut) *errOut = "无效的 IP 地址: " + m_host;
        return false;
    }
    if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        closesocket(s);
        if (errOut) *errOut = QString("连接 %1:%2 超时或被拒绝").arg(m_host).arg(targetPort);
        return false;
    }
    m_sock = s;
#else
    int s = socket(AF_INET, SOCK_STREAM, 0);
    if (s < 0) return false;
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(targetPort));
    inet_pton(AF_INET, m_host.toUtf8().constData(), &addr.sin_addr);
    if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close(s);
        return false;
    }
    m_sock = s;
#endif

    if (m_protocol == "Siemens S7") {
        uint8_t tsapDst = static_cast<uint8_t>((m_rack << 5) | (m_slot & 0x1F));
        uint8_t cotpCr[22] = {
            0x03, 0x00, 0x00, 0x16,
            0x11, 0xE0, 0x00, 0x00, 0x00, 0x01, 0x00,
            0xC0, 0x01, 0x0A,
            0xC1, 0x02, 0x01, 0x00,
            0xC2, 0x02, 0x01, tsapDst
        };
        uint8_t cotpResp[22]{};
        if (!sendAll(&m_sock, cotpCr, 22) || !recvExact(&m_sock, cotpResp, 22) || cotpResp[5] != 0xD0) {
            disconnectUnlocked();
            if (errOut) *errOut = "S7 COTP 握手失败，请检查机架号/槽号";
            return false;
        }
        uint8_t s7Setup[25] = {
            0x03, 0x00, 0x00, 0x19, 0x02, 0xF0, 0x80,
            0x32, 0x01, 0x00, 0x00, 0x00, 0x01, 0x00, 0x08, 0x00, 0x00,
            0xF0, 0x00, 0x00, 0x01, 0x00, 0x01, 0x01, 0xE0
        };
        uint8_t setupResp[27]{};
        if (!sendAll(&m_sock, s7Setup, 25) || !recvExact(&m_sock, setupResp, 27)) {
            disconnectUnlocked();
            if (errOut) *errOut = "S7 通信参数协商失败，请确认PLC已开启PUT/GET访问权限";
            return false;
        }
    }

    m_connected.store(true);
    return true;
}

bool PlcClient::sendAll(void* sockPtr, const uint8_t* buf, int len) {
#ifdef _WIN32
    SOCKET s = *reinterpret_cast<SOCKET*>(sockPtr);
    int sent = 0;
    while (sent < len) {
        int r = ::send(s, reinterpret_cast<const char*>(buf + sent), len - sent, 0);
        if (r <= 0) return false;
        sent += r;
    }
    return true;
#else
    int s = *reinterpret_cast<int*>(sockPtr);
    int sent = 0;
    while (sent < len) {
        int r = static_cast<int>(::send(s, buf + sent, len - sent, 0));
        if (r <= 0) return false;
        sent += r;
    }
    return true;
#endif
}

bool PlcClient::recvExact(void* sockPtr, uint8_t* buf, int len) {
#ifdef _WIN32
    SOCKET s = *reinterpret_cast<SOCKET*>(sockPtr);
    int got = 0;
    while (got < len) {
        int r = ::recv(s, reinterpret_cast<char*>(buf + got), len - got, 0);
        if (r <= 0) return false;
        got += r;
    }
    return true;
#else
    int s = *reinterpret_cast<int*>(sockPtr);
    int got = 0;
    while (got < len) {
        int r = static_cast<int>(::recv(s, buf + got, len - got, 0));
        if (r <= 0) return false;
        got += r;
    }
    return true;
#endif
}

bool PlcClient::readRegisters(int addr, int count, QVector<int16_t>& outValues, QString* errOut) {
    std::lock_guard<std::mutex> lk(m_mutex);
    outValues.clear();
    if (!m_connected.load()) {
        if (errOut) *errOut = "PLC 未连接";
        return false;
    }

    if (m_protocol == "Modbus TCP") {
        uint16_t tid = ++m_tid;
        uint8_t req[12] = {
            static_cast<uint8_t>(tid >> 8), static_cast<uint8_t>(tid & 0xFF),
            0x00, 0x00, 0x00, 0x06,
            static_cast<uint8_t>(m_unitId), 0x03,
            static_cast<uint8_t>(addr >> 8), static_cast<uint8_t>(addr & 0xFF),
            static_cast<uint8_t>(count >> 8), static_cast<uint8_t>(count & 0xFF)
        };
        if (!sendAll(&m_sock, req, 12)) {
            if (errOut) *errOut = "Modbus TCP 发送读取请求失败";
            return false;
        }
        m_txCount++;
        uint8_t hdr[7]{};
        if (!recvExact(&m_sock, hdr, 7)) {
            if (errOut) *errOut = "Modbus TCP 接收响应头超时";
            return false;
        }
        int bodyLen = ((static_cast<int>(hdr[4]) << 8) | hdr[5]) - 1;
        if (bodyLen <= 0 || bodyLen > 512) {
            if (errOut) *errOut = "Modbus TCP 报文长度异常";
            return false;
        }
        std::vector<uint8_t> body(bodyLen);
        if (!recvExact(&m_sock, body.data(), bodyLen)) {
            if (errOut) *errOut = "Modbus TCP 接收数据体超时";
            return false;
        }
        m_rxCount++;
        if (body[0] & 0x80) {
            if (errOut) *errOut = QString("Modbus 异常码: %1").arg(body[1]);
            return false;
        }
        int bytes = body[1];
        for (int i = 0; i + 1 < bytes; i += 2) {
            uint16_t u = (static_cast<uint16_t>(body[2 + i]) << 8) | body[3 + i];
            outValues.push_back(static_cast<int16_t>(u));
        }
        return true;
    }

    if (m_protocol == "Modbus RTU") {
#ifdef _WIN32
        uint8_t req[8] = {
            static_cast<uint8_t>(m_unitId), 0x03,
            static_cast<uint8_t>(addr >> 8), static_cast<uint8_t>(addr & 0xFF),
            static_cast<uint8_t>(count >> 8), static_cast<uint8_t>(count & 0xFF),
            0, 0
        };
        uint16_t crc = calcCrc16(req, 6);
        req[6] = static_cast<uint8_t>(crc & 0xFF);
        req[7] = static_cast<uint8_t>(crc >> 8);
        PurgeComm(m_serial, PURGE_RXCLEAR);
        DWORD wr = 0;
        if (!WriteFile(m_serial, req, 8, &wr, nullptr) || wr != 8) {
            if (errOut) *errOut = "Modbus RTU 串口写入失败";
            return false;
        }
        m_txCount++;
        int expLen = 5 + count * 2;
        std::vector<uint8_t> resp(expLen);
        DWORD rd = 0;
        if (!ReadFile(m_serial, resp.data(), expLen, &rd, nullptr) || rd < 5) {
            if (errOut) *errOut = "Modbus RTU 串口读取超时";
            return false;
        }
        m_rxCount++;
        if (resp[1] & 0x80) {
            if (errOut) *errOut = QString("Modbus RTU 异常码: %1").arg(resp[2]);
            return false;
        }
        int bytes = resp[2];
        for (int i = 0; i + 1 < bytes && (3 + i + 1) < static_cast<int>(rd); i += 2) {
            uint16_t u = (static_cast<uint16_t>(resp[3 + i]) << 8) | resp[4 + i];
            outValues.push_back(static_cast<int16_t>(u));
        }
        return true;
#endif
    }

    if (m_protocol == "Siemens S7") {
        uint16_t tid = ++m_tid;
        uint16_t byteCount = static_cast<uint16_t>(count * 2);
        uint32_t bitOffset = static_cast<uint32_t>(addr * 8);
        uint8_t req[31] = {
            0x03, 0x00, 0x00, 0x1F, 0x02, 0xF0, 0x80,
            0x32, 0x01, 0x00, 0x00,
            static_cast<uint8_t>(tid >> 8), static_cast<uint8_t>(tid & 0xFF),
            0x00, 0x0E, 0x00, 0x00,
            0x04, 0x01,
            0x12, 0x0A, 0x10, 0x02,
            static_cast<uint8_t>(byteCount >> 8), static_cast<uint8_t>(byteCount & 0xFF),
            static_cast<uint8_t>(m_dbNum >> 8), static_cast<uint8_t>(m_dbNum & 0xFF),
            0x84,
            static_cast<uint8_t>((bitOffset >> 16) & 0xFF),
            static_cast<uint8_t>((bitOffset >> 8) & 0xFF),
            static_cast<uint8_t>(bitOffset & 0xFF)
        };
        if (!sendAll(&m_sock, req, 31)) {
            if (errOut) *errOut = "S7 发送读取请求失败";
            return false;
        }
        m_txCount++;
        uint8_t tpkt[4]{};
        if (!recvExact(&m_sock, tpkt, 4)) {
            if (errOut) *errOut = "S7 读取响应超时";
            return false;
        }
        int totalLen = (static_cast<int>(tpkt[2]) << 8) | tpkt[3];
        if (totalLen < 25) {
            if (errOut) *errOut = "S7 响应长度异常";
            return false;
        }
        std::vector<uint8_t> body(totalLen - 4);
        if (!recvExact(&m_sock, body.data(), totalLen - 4)) return false;
        m_rxCount++;
        if (body[17] != 0xFF) {
            if (errOut) *errOut = QString("S7 DB%1 读取返回错误码: 0x%2").arg(m_dbNum).arg(body[17], 2, 16, QChar('0'));
            return false;
        }
        for (int i = 0; i < count; ++i) {
            int idx = 21 + i * 2;
            if (idx + 1 < static_cast<int>(body.size())) {
                uint16_t u = (static_cast<uint16_t>(body[idx]) << 8) | body[idx + 1];
                outValues.push_back(static_cast<int16_t>(u));
            }
        }
        return true;
    }

    if (errOut) *errOut = "当前协议不支持寄存器按址读取";
    return false;
}

bool PlcClient::writeRegisters(int addr, const QVector<uint16_t>& words, QString* errOut) {
    std::lock_guard<std::mutex> lk(m_mutex);
    if (!m_connected.load()) {
        if (errOut) *errOut = "PLC 未连接";
        return false;
    }
    if (words.isEmpty()) return true;

    if (m_protocol == "Modbus TCP") {
        uint16_t tid = ++m_tid;
        int count = words.size();
        uint8_t byteCnt = static_cast<uint8_t>(count * 2);
        uint16_t pduLen = static_cast<uint16_t>(7 + byteCnt);
        std::vector<uint8_t> req(6 + pduLen);
        req[0] = static_cast<uint8_t>(tid >> 8);
        req[1] = static_cast<uint8_t>(tid & 0xFF);
        req[2] = 0x00; req[3] = 0x00;
        req[4] = static_cast<uint8_t>(pduLen >> 8);
        req[5] = static_cast<uint8_t>(pduLen & 0xFF);
        req[6] = static_cast<uint8_t>(m_unitId);
        req[7] = 0x10;
        req[8] = static_cast<uint8_t>(addr >> 8);
        req[9] = static_cast<uint8_t>(addr & 0xFF);
        req[10] = static_cast<uint8_t>(count >> 8);
        req[11] = static_cast<uint8_t>(count & 0xFF);
        req[12] = byteCnt;
        for (int i = 0; i < count; ++i) {
            req[13 + i * 2] = static_cast<uint8_t>(words[i] >> 8);
            req[14 + i * 2] = static_cast<uint8_t>(words[i] & 0xFF);
        }
        if (!sendAll(&m_sock, req.data(), static_cast<int>(req.size()))) {
            if (errOut) *errOut = "Modbus TCP 写入发送失败";
            return false;
        }
        m_txCount++;
        uint8_t resp[12]{};
        if (!recvExact(&m_sock, resp, 12)) {
            if (errOut) *errOut = "Modbus TCP 写入确认超时";
            return false;
        }
        m_rxCount++;
        if (resp[7] & 0x80) {
            if (errOut) *errOut = QString("Modbus TCP 写入异常码: %1").arg(resp[8]);
            return false;
        }
        return true;
    }

    if (m_protocol == "Modbus RTU") {
#ifdef _WIN32
        int count = words.size();
        uint8_t byteCnt = static_cast<uint8_t>(count * 2);
        std::vector<uint8_t> req(9 + byteCnt);
        req[0] = static_cast<uint8_t>(m_unitId);
        req[1] = 0x10;
        req[2] = static_cast<uint8_t>(addr >> 8);
        req[3] = static_cast<uint8_t>(addr & 0xFF);
        req[4] = static_cast<uint8_t>(count >> 8);
        req[5] = static_cast<uint8_t>(count & 0xFF);
        req[6] = byteCnt;
        for (int i = 0; i < count; ++i) {
            req[7 + i * 2] = static_cast<uint8_t>(words[i] >> 8);
            req[8 + i * 2] = static_cast<uint8_t>(words[i] & 0xFF);
        }
        uint16_t crc = calcCrc16(req.data(), 7 + byteCnt);
        req[7 + byteCnt] = static_cast<uint8_t>(crc & 0xFF);
        req[8 + byteCnt] = static_cast<uint8_t>(crc >> 8);
        PurgeComm(m_serial, PURGE_RXCLEAR);
        DWORD wr = 0;
        if (!WriteFile(m_serial, req.data(), static_cast<DWORD>(req.size()), &wr, nullptr)) {
            if (errOut) *errOut = "Modbus RTU 写入失败";
            return false;
        }
        m_txCount++;
        uint8_t resp[8]{};
        DWORD rd = 0;
        if (!ReadFile(m_serial, resp, 8, &rd, nullptr) || rd < 5 || (resp[1] & 0x80)) {
            if (errOut) *errOut = "Modbus RTU 写入无响应或异常";
            return false;
        }
        m_rxCount++;
        return true;
#endif
    }

    if (m_protocol == "Siemens S7") {
        uint16_t tid = ++m_tid;
        uint16_t byteCount = static_cast<uint16_t>(words.size() * 2);
        uint16_t bitLen = static_cast<uint16_t>(byteCount * 8);
        uint32_t bitOffset = static_cast<uint32_t>(addr * 8);
        uint16_t dataParamLen = static_cast<uint16_t>(4 + byteCount);
        uint16_t tpktLen = static_cast<uint16_t>(35 + byteCount);

        std::vector<uint8_t> req(tpktLen, 0);
        req[0] = 0x03; req[1] = 0x00;
        req[2] = static_cast<uint8_t>(tpktLen >> 8);
        req[3] = static_cast<uint8_t>(tpktLen & 0xFF);
        req[4] = 0x02; req[5] = 0xF0; req[6] = 0x80;
        req[7] = 0x32; req[8] = 0x01;
        req[11] = static_cast<uint8_t>(tid >> 8);
        req[12] = static_cast<uint8_t>(tid & 0xFF);
        req[13] = 0x00; req[14] = 0x0E;
        req[15] = static_cast<uint8_t>(dataParamLen >> 8);
        req[16] = static_cast<uint8_t>(dataParamLen & 0xFF);
        req[17] = 0x05; req[18] = 0x01;
        req[19] = 0x12; req[20] = 0x0A; req[21] = 0x10; req[22] = 0x02;
        req[23] = static_cast<uint8_t>(byteCount >> 8);
        req[24] = static_cast<uint8_t>(byteCount & 0xFF);
        req[25] = static_cast<uint8_t>(m_dbNum >> 8);
        req[26] = static_cast<uint8_t>(m_dbNum & 0xFF);
        req[27] = 0x84;
        req[28] = static_cast<uint8_t>((bitOffset >> 16) & 0xFF);
        req[29] = static_cast<uint8_t>((bitOffset >> 8) & 0xFF);
        req[30] = static_cast<uint8_t>(bitOffset & 0xFF);
        req[31] = 0x00; req[32] = 0x04;
        req[33] = static_cast<uint8_t>(bitLen >> 8);
        req[34] = static_cast<uint8_t>(bitLen & 0xFF);
        for (int i = 0; i < words.size(); ++i) {
            req[35 + i * 2] = static_cast<uint8_t>(words[i] >> 8);
            req[36 + i * 2] = static_cast<uint8_t>(words[i] & 0xFF);
        }
        if (!sendAll(&m_sock, req.data(), tpktLen)) {
            if (errOut) *errOut = "S7 写入发送失败";
            return false;
        }
        m_txCount++;
        uint8_t resp[22]{};
        if (!recvExact(&m_sock, resp, 22) || resp[21] != 0xFF) {
            if (errOut) *errOut = "S7 DB 写入失败或被拒绝";
            return false;
        }
        m_rxCount++;
        return true;
    }

    if (m_protocol == "TCP Socket") {
        QStringList parts;
        for (uint16_t w : words) parts << QString::number(static_cast<int16_t>(w));
        QByteArray msg = (parts.join(",") + "\r\n").toUtf8();
        if (!sendAll(&m_sock, reinterpret_cast<const uint8_t*>(msg.constData()), msg.size())) {
            if (errOut) *errOut = "TCP Socket 发送失败";
            return false;
        }
        m_txCount++;
        return true;
    }

    return false;
}

bool PlcClient::writeMappedWords(const QVector<AddrWordWrite>& items, QString* errOut) {
    if (items.isEmpty()) return true;

    if (protocol() == "TCP Socket") {
        QVector<uint16_t> allWords;
        for (const auto& it : items) allWords.push_back(it.word);
        return writeRegisters(items.first().addr, allWords, errOut);
    }

    int idx = 0;
    int stepBytes = (protocol() == "Siemens S7") ? 2 : 1;
    while (idx < items.size()) {
        int startAddr = items[idx].addr;
        QVector<uint16_t> chunk;
        chunk.push_back(items[idx].word);
        int nextExpectedAddr = startAddr + stepBytes;
        ++idx;
        while (idx < items.size() && items[idx].addr == nextExpectedAddr && chunk.size() < 60) {
            chunk.push_back(items[idx].word);
            nextExpectedAddr += stepBytes;
            ++idx;
        }
        if (!writeRegisters(startAddr, chunk, errOut)) {
            return false;
        }
    }
    return true;
}

bool PlcClient::sendRawText(const QString& text, QString* replyOut, QString* errOut) {
    std::lock_guard<std::mutex> lk(m_mutex);
    if (!m_connected.load() || m_protocol != "TCP Socket") {
        if (errOut) *errOut = "仅在 TCP Socket 连接模式下支持自由口文本收发";
        return false;
    }
    QByteArray payload = (text + "\r\n").toUtf8();
    if (!sendAll(&m_sock, reinterpret_cast<const uint8_t*>(payload.constData()), payload.size())) {
        if (errOut) *errOut = "TCP 发送失败";
        return false;
    }
    m_txCount++;
    if (replyOut) *replyOut = "";
    return true;
}
