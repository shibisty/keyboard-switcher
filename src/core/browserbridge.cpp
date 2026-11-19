#include "browserbridge.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QTimer>

namespace {
constexpr int kMaxRequest = 8 * 1024;

QByteArray headerValue(const QList<QByteArray> &lines, const QByteArray &name)
{
    for (const QByteArray &line : lines) {
        const int colon = line.indexOf(':');
        if (colon > 0 && line.left(colon).trimmed().toLower() == name)
            return line.mid(colon + 1).trimmed();
    }
    return {};
}

bool originAllowed(const QByteArray &origin)
{
    // Extensions send their own scheme; web pages send http(s) and must not drive us.
    return origin.isEmpty() || origin.startsWith("chrome-extension://") || origin.startsWith("moz-extension://")
        || origin.startsWith("safari-web-extension://") || origin.startsWith("extension://");
}
} // namespace

BrowserBridge::BrowserBridge(QObject *parent)
    : QObject(parent)
{
    connect(&m_server, &QTcpServer::newConnection, this, &BrowserBridge::onNewConnection);
}

bool BrowserBridge::listen(quint16 port)
{
    m_port = port;
    if (m_server.isListening())
        m_server.close();
    return m_server.listen(QHostAddress::LocalHost, port);
}

void BrowserBridge::onNewConnection()
{
    while (QTcpSocket *socket = m_server.nextPendingConnection()) {
        m_buffers.insert(socket, {});
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { onReadyRead(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            m_buffers.remove(socket);
            socket->deleteLater();
        });
        QTimer::singleShot(5000, socket, [socket] { socket->abort(); });
    }
}

void BrowserBridge::reply(QTcpSocket *socket, const QByteArray &status, const QByteArray &body)
{
    QByteArray response = "HTTP/1.1 " + status + "\r\n"
                          "Access-Control-Allow-Origin: *\r\n"
                          "Access-Control-Allow-Methods: POST, GET, OPTIONS\r\n"
                          "Access-Control-Allow-Headers: Content-Type\r\n"
                          "Content-Type: text/plain\r\n"
                          "Connection: close\r\n"
                          "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
    socket->write(response);
    socket->disconnectFromHost();
}

void BrowserBridge::onReadyRead(QTcpSocket *socket)
{
    QByteArray &buf = m_buffers[socket];
    buf += socket->readAll();
    if (buf.size() > kMaxRequest) {
        socket->abort();
        return;
    }
    const int headerEnd = buf.indexOf("\r\n\r\n");
    if (headerEnd < 0)
        return;

    const QList<QByteArray> lines = buf.left(headerEnd).split('\n');
    const QList<QByteArray> request = lines.value(0).trimmed().split(' ');
    const QByteArray method = request.value(0);
    const QByteArray path = request.value(1);
    const int contentLength = headerValue(lines, "content-length").toInt();
    if (buf.size() < headerEnd + 4 + contentLength)
        return; // wait for the body

    if (!originAllowed(headerValue(lines, "origin"))) {
        reply(socket, "403 Forbidden");
        return;
    }
    if (method == "OPTIONS") {
        reply(socket, "204 No Content");
        return;
    }
    if (method == "GET" && path == "/ping") {
        reply(socket, "200 OK", "keyboard-changer");
        return;
    }
    if (method != "POST" || path != "/tab") {
        reply(socket, "404 Not Found");
        return;
    }

    const QByteArray body = buf.mid(headerEnd + 4, contentLength);
    const QJsonObject json = QJsonDocument::fromJson(body).object();
    QString domain = json.value(QLatin1String("domain")).toString().trimmed().toLower();
    static const QRegularExpression valid(QStringLiteral("^[a-z0-9.-]{0,253}$"));
    if (!valid.match(domain).hasMatch()) {
        reply(socket, "400 Bad Request");
        return;
    }
    if (domain.startsWith(QLatin1String("www.")))
        domain.remove(0, 4);
    reply(socket, "204 No Content");
    emit domainReported(domain);
}
