#pragma once

#include <QObject>
#include <QJsonObject>
#include <QTcpServer>

#include <functional>

class QTcpSocket;

namespace draw_stats {

class ObsStandaloneOverlayServer final : public QObject {
	Q_OBJECT

public:
	explicit ObsStandaloneOverlayServer(std::function<QJsonObject()> stateProvider, QObject *parent = nullptr);

	bool start();
	void stop();
	quint16 port() const;
	QString overlayUrl() const;
	QString beginAuthCallbackUrl();

signals:
	void authTokenReceived(const QString &token);
	void authFailed(const QString &reason);

private:
	void acceptConnection();
	void handleRequest(QTcpSocket *socket);
	void writeResponse(QTcpSocket *socket, int status, const QByteArray &contentType, const QByteArray &body);

	QTcpServer server_;
	std::function<QJsonObject()> stateProvider_;
	QByteArray overlayHtml_;
	QString expectedAuthState_;
	qint64 authStateExpiresAtMs_ = 0;
};

} // namespace draw_stats
