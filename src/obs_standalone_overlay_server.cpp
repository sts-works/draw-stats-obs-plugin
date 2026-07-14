#include "obs_standalone_overlay_server.hpp"

#include "obs_standalone_security.hpp"

#include <QDateTime>
#include <QFile>
#include <QHostAddress>
#include <QJsonDocument>
#include <QTcpSocket>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>

#include <obs-module.h>

namespace draw_stats {

namespace {

QByteArray statusText(int status)
{
	switch (status) {
	case 200:
		return "OK";
	case 404:
		return "Not Found";
	default:
		return "Bad Request";
	}
}

QByteArray defaultOverlayHtml()
{
	return "<!doctype html><meta charset=\"utf-8\"><style>html,body{background:transparent;color:white;font:16px sans-serif}</style>"
	       "<body>Draw Stats overlay asset is unavailable.</body>";
}

} // namespace

ObsStandaloneOverlayServer::ObsStandaloneOverlayServer(std::function<QJsonObject()> stateProvider, QObject *parent)
	: QObject(parent),
	  stateProvider_(std::move(stateProvider))
{
	connect(&server_, &QTcpServer::newConnection, this, &ObsStandaloneOverlayServer::acceptConnection);
}

bool ObsStandaloneOverlayServer::start()
{
	if (server_.isListening())
		return true;

	char *path = obs_module_file("overlay/index.html");
	if (path) {
		QFile file(QString::fromUtf8(path));
		if (file.open(QIODevice::ReadOnly))
			overlayHtml_ = file.readAll();
		bfree(path);
	}
	if (overlayHtml_.isEmpty())
		overlayHtml_ = defaultOverlayHtml();

	return server_.listen(QHostAddress::LocalHost, 0);
}

void ObsStandaloneOverlayServer::stop()
{
	server_.close();
}

quint16 ObsStandaloneOverlayServer::port() const
{
	return server_.serverPort();
}

QString ObsStandaloneOverlayServer::overlayUrl() const
{
	return QStringLiteral("http://127.0.0.1:%1/overlay").arg(port());
}

QString ObsStandaloneOverlayServer::beginAuthCallbackUrl()
{
	expectedAuthState_ =
		QUuid::createUuid().toString(QUuid::WithoutBraces) + QUuid::createUuid().toString(QUuid::WithoutBraces);
	authStateExpiresAtMs_ = QDateTime::currentMSecsSinceEpoch() + 10 * 60 * 1000;
	QUrl callback(QStringLiteral("http://127.0.0.1:%1/desktop-auth-callback").arg(port()));
	QUrlQuery query;
	query.addQueryItem(QStringLiteral("state"), expectedAuthState_);
	callback.setQuery(query);
	return callback.toString(QUrl::FullyEncoded);
}

void ObsStandaloneOverlayServer::acceptConnection()
{
	while (QTcpSocket *socket = server_.nextPendingConnection()) {
		socket->setParent(this);
		connect(socket, &QTcpSocket::readyRead, this, [this, socket] { handleRequest(socket); });
		connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
	}
}

void ObsStandaloneOverlayServer::handleRequest(QTcpSocket *socket)
{
	const QByteArray request = socket->readAll();
	const qsizetype lineEnd = request.indexOf("\r\n");
	if (lineEnd < 0)
		return;
	const QList<QByteArray> requestLine = request.left(lineEnd).split(' ');
	if (requestLine.size() < 2 || requestLine.at(0) != "GET") {
		writeResponse(socket, 400, "text/plain; charset=utf-8", "Bad request");
		return;
	}

	const QUrl url = QUrl::fromEncoded(requestLine.at(1));
	if (url.path() == "/overlay") {
		writeResponse(socket, 200, "text/html; charset=utf-8", overlayHtml_);
		return;
	}
	if (url.path() == "/state") {
		writeResponse(socket, 200, "application/json; charset=utf-8",
			      jsonResponse(sanitizeOverlayState(stateProvider_())));
		return;
	}
	if (url.path() == "/desktop-auth-callback") {
		const QUrlQuery query(url);
		const qint64 now = QDateTime::currentMSecsSinceEpoch();
		const QString callbackState = query.queryItemValue("state");
		if (!isValidAuthCallbackState(expectedAuthState_, callbackState, authStateExpiresAtMs_, now)) {
			if (now > authStateExpiresAtMs_) {
				expectedAuthState_.clear();
				authStateExpiresAtMs_ = 0;
			}
			writeResponse(socket, 400, "text/html; charset=utf-8",
				      "Sign-in verification failed. Return to OBS Studio and retry.");
			return;
		}
		expectedAuthState_.clear();
		authStateExpiresAtMs_ = 0;
		const QString token = query.queryItemValue("session_token", QUrl::FullyDecoded);
		const QString status = query.queryItemValue("status");
		if (status == "signed_in" && !token.isEmpty()) {
			emit authTokenReceived(token);
			writeResponse(
				socket, 200, "text/html; charset=utf-8",
				"<!doctype html><meta charset=\"utf-8\"><title>Draw Stats</title>"
				"<style>body{margin:0;background:#151517;color:#fff;font:16px -apple-system,BlinkMacSystemFont,sans-serif;display:grid;place-items:center;height:100vh}"
				".p{border:1px solid #333;padding:28px 34px;border-radius:8px;background:#1f1f21}.ok{color:#ff9b32}</style>"
				"<div class=\"p\"><strong class=\"ok\">Signed in</strong><p>You can return to OBS Studio.</p></div>");
		} else {
			emit authFailed(QStringLiteral("The provider did not return a valid session."));
			writeResponse(socket, 400, "text/html; charset=utf-8",
				      "Sign-in could not be completed. Return to OBS Studio and retry.");
		}
		return;
	}
	writeResponse(socket, 404, "text/plain; charset=utf-8", "Not found");
}

void ObsStandaloneOverlayServer::writeResponse(QTcpSocket *socket, int status, const QByteArray &contentType,
					       const QByteArray &body)
{
	QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + " " + statusText(status) + "\r\n";
	response += "Content-Type: " + contentType + "\r\n";
	response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
	response += "Cache-Control: no-store\r\n";
	response += "X-Content-Type-Options: nosniff\r\n";
	response += "Connection: close\r\n\r\n";
	response += body;
	socket->write(response);
	socket->disconnectFromHost();
}

} // namespace draw_stats
