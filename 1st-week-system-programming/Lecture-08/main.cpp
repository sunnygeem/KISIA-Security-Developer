#include "pch.h"

constexpr std::uint32_t REQ_RESUME = 1001;
constexpr std::uint32_t CMD_RESUME_ACK = 1002;
constexpr std::uint32_t REQ_INQUIRY = 1003;
constexpr std::uint32_t CMD_INQUIRY = 1004;
constexpr std::uint32_t REQ_ANSWER = 1005;
constexpr std::uint32_t CMD_ANSWER_ACK = 1006;
constexpr std::uint32_t QUESTION_INDEX_BASE = 0;

void writeLE32(char* dst, std::uint32_t value)
{
    dst[0] = static_cast<char>(value & 0xFF);
    dst[1] = static_cast<char>((value >> 8) & 0xFF);
    dst[2] = static_cast<char>((value >> 16) & 0xFF);
    dst[3] = static_cast<char>((value >> 24) & 0xFF);
}

std::uint32_t readLE32(const char* src)
{
    return static_cast<std::uint32_t>(static_cast<unsigned char>(src[0])) |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(src[1])) << 8) |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(src[2])) << 16) |
        (static_cast<std::uint32_t>(static_cast<unsigned char>(src[3])) << 24);
}

bool sendAll(SOCKET sock, const char* data, int len)
{
    int total = 0;
    while (total < len)
    {
        int sent = send(sock, data + total, len - total, 0);
        if (sent == SOCKET_ERROR || sent == 0)
            return false;
        total += sent;
    }
    return true;
}

bool recvAll(SOCKET sock, char* data, int len)
{
    int total = 0;
    while (total < len)
    {
        int recvd = recv(sock, data + total, len - total, 0);
        if (recvd == SOCKET_ERROR || recvd == 0)
            return false;
        total += recvd;
    }
    return true;
}

bool sendPacket(SOCKET sock, std::uint32_t packetId, const std::string& body)
{
    char header[8];
    writeLE32(header, packetId);
    writeLE32(header + 4, static_cast<std::uint32_t>(body.size()));

    if (!sendAll(sock, header, 8))
        return false;

    if (!body.empty() && !sendAll(sock, body.data(), static_cast<int>(body.size())))
        return false;

    return true;
}

bool recvPacket(SOCKET sock, std::uint32_t& packetId, std::string& body)
{
    char header[8];
    if (!recvAll(sock, header, 8))
        return false;

    packetId = readLE32(header);
    std::uint32_t bodyLen = readLE32(header + 4);

    if (bodyLen > 1024 * 1024)
    {
        std::cerr << "Body too large: " << bodyLen << " bytes\n";
        return false;
    }

    std::vector<char> buffer(bodyLen);
    if (bodyLen > 0 && !recvAll(sock, buffer.data(), static_cast<int>(bodyLen)))
        return false;

    body.assign(buffer.begin(), buffer.end());
    return true;
}

std::string removeWhitespace(const std::string& input)
{
    std::string out;
    out.reserve(input.size());

    for (unsigned char c : input)
    {
        if (!std::isspace(c))
            out.push_back(static_cast<char>(c));
    }

    return out;
}

bool ackSucceeded(const std::string& body)
{
    const std::string compact = removeWhitespace(body);
    return compact.find("\"ErrorCode\":0") != std::string::npos;
}

std::size_t countOccurrences(const std::string& text, const std::string& token)
{
    std::size_t count = 0;
    std::size_t pos = 0;

    while ((pos = text.find(token, pos)) != std::string::npos)
    {
        ++count;
        pos += token.size();
    }

    return count;
}

std::string escapeJsonString(const std::string& input)
{
    std::string out;
    out.reserve(input.size() + 16);

    for (unsigned char c : input)
    {
        switch (c)
        {
        case '\"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (c < 0x20)
            {
                char buf[7];
                std::snprintf(buf, sizeof(buf), "\\u%04X", c);
                out += buf;
            }
            else
            {
                out += static_cast<char>(c);
            }
            break;
        }
    }

    return out;
}

std::string buildAnswerJson(std::uint32_t questionIdx, const std::string& answer)
{
    return std::string("{\"QuestionIdx\":") +
        std::to_string(questionIdx) +
        ",\"Answer\":\"" +
        escapeJsonString(answer) +
        "\"}";
}

int main()
{
    setlocale(LC_ALL, "");

    WSADATA wsaData{};
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        std::cerr << "WSAStartup failed.\n";
        return 1;
    }

    SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET)
    {
        std::cerr << "socket() failed. error=" << WSAGetLastError() << "\n";
        WSACleanup();
        return 1;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(SERVER_PORT);

    if (inet_pton(AF_INET, SERVER_IP, &serverAddr.sin_addr) != 1)
    {
        std::cerr << "Invalid server IP address.\n";
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    if (connect(sock, reinterpret_cast<sockaddr*>(&serverAddr), sizeof(serverAddr)) == SOCKET_ERROR)
    {
        std::cerr << "connect() failed. error=" << WSAGetLastError() << "\n";
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    std::cout << "Connected to " << SERVER_IP << ":" << SERVER_PORT << "\n";

    const std::string resumeJson =
        R"({"Resume":{"Name":"","Greeting":"Hello","Introduction":"Code for TCP socket lab"}})";

    if (!sendPacket(sock, REQ_RESUME, resumeJson))
    {
        std::cerr << "Failed to send REQ_RESUME. error=" << WSAGetLastError() << "\n";
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    std::uint32_t packetId = 0;
    std::string responseBody;

    if (!recvPacket(sock, packetId, responseBody))
    {
        std::cerr << "Failed to receive CMD_RESUME_ACK. error=" << WSAGetLastError() << "\n";
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    std::cout << "[Resume ACK] PacketId=" << packetId << "\n";
    std::cout << responseBody << "\n\n";

    if (packetId != CMD_RESUME_ACK || !ackSucceeded(responseBody))
    {
        std::cerr << "Resume was not accepted.\n";
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    const std::string inquiryJson =
        R"({"QuestionCode":"s-developer 4th"})";

    if (!sendPacket(sock, REQ_INQUIRY, inquiryJson))
    {
        std::cerr << "Failed to send REQ_INQUIRY. error=" << WSAGetLastError() << "\n";
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    if (!recvPacket(sock, packetId, responseBody))
    {
        std::cerr << "Failed to receive CMD_INQUIRY. error=" << WSAGetLastError() << "\n";
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    std::cout << "[Inquiry Response] PacketId=" << packetId << "\n";
    std::cout << responseBody << "\n\n";

    if (packetId != CMD_INQUIRY)
    {
        std::cerr << "Unexpected packet. Expected 1004.\n";
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    const std::vector<std::string> answers =
    {
        "\/some\\\\path",
        "가급적 사용하지 않는 것이 좋다.",
        "char형은 유니코드 문자열이 아니다.",
        "대표헤더 파일에서 공통헤더 파일을 include 한다.",
        "이 오류는 링커에서 발생한다.",
        "OPEN_ALWAYS는 지정한 경로에 파일이 없을 경우에 실패한다.",
        "ZVWXYZVWXYZVWXYZVWXYZVWXYZ",
        "map은 key-value 쌍들로 이루어져 있으며 begin에는 가장 작은 키의 원소를 end에는 가장 큰 키의 원소를 조회할 수있다.",
        "Serializer는 Formatter의 하위 개념으로 parser와 동일한 의미를 가진다.",
        "UDP 소켓에서 recv할 때 0이 반환되는 경우가 존재한다.(상대방이 0바이트를 send한 경우는 제외)"
    };

    const std::size_t receivedQuestionCount = countOccurrences(responseBody, "\"Question\"");
    if (receivedQuestionCount != answers.size())
    {
        std::cerr << "Warning: server returned " << receivedQuestionCount
            << " questions, but this client has " << answers.size()
            << " prepared answers.\n";
    }

    for (std::size_t i = 0; i < answers.size(); ++i)
    {
        const std::uint32_t questionIdx =
            QUESTION_INDEX_BASE + static_cast<std::uint32_t>(i);

        const std::string answerJson = buildAnswerJson(questionIdx, answers[i]);

        std::cout << "[Sending Answer] QuestionIdx=" << questionIdx << "\n";
        std::cout << answerJson << "\n";

        if (!sendPacket(sock, REQ_ANSWER, answerJson))
        {
            std::cerr << "Failed to send REQ_ANSWER for QuestionIdx="
                << questionIdx << ". error=" << WSAGetLastError() << "\n";
            closesocket(sock);
            WSACleanup();
            return 1;
        }

        if (!recvPacket(sock, packetId, responseBody))
        {
            std::cerr << "Failed to receive CMD_ANSWER_ACK for QuestionIdx="
                << questionIdx << ". error=" << WSAGetLastError() << "\n";
            closesocket(sock);
            WSACleanup();
            return 1;
        }

        std::cout << "[Answer ACK] PacketId=" << packetId << "\n";
        std::cout << responseBody << "\n\n";

        if (packetId != CMD_ANSWER_ACK)
        {
            std::cerr << "Unexpected packet after answer. Expected 1006.\n";
            closesocket(sock);
            WSACleanup();
            return 1;
        }

        if (!ackSucceeded(responseBody))
        {
            std::cerr << "Server did not accept answer for QuestionIdx="
                << questionIdx << ".\n";
            closesocket(sock);
            WSACleanup();
            return 1;
        }
    }

    std::cout << "All answers sent.\n";

    closesocket(sock);
    WSACleanup();
    return 0;
}