#pragma once

#include <QHash>
#include <QObject>
#include <QTcpServer>

class QTcpSocket;

// Tiny HTTP endpoint on 127.0.0.1 for the browser extension.
// The extension POSTs {"domain": "github.com"} when the active tab changes.
// Only the domain is sent; requests from web pages (http/https Origin) are rejected.
class BrowserBridge : public QObject
{
    Q_OBJECT
public:
    static constexpr quint16 DefaultPort = 47863;

    explicit BrowserBridge(QObject *parent = nullptr);

    bool listen(quint16 port);
    bool isListening() const { return m_server.isListening(); }
    quint16 port() const { return m_port; }

signals:
    void domainReported(const QString &domain);

private:
    void onNewConnection();
    void onReadyRead(QTcpSocket *socket);
    void reply(QTcpSocket *socket, const QByteArray &status, const QByteArray &body = {});

    QTcpServer m_server;
    quint16 m_port = DefaultPort;
    QHash<QTcpSocket *, QByteArray> m_buffers;
};
