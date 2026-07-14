#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>

namespace draw_stats {

QString normalizeProcessIdentity(const QString &value);
QString escapeHtml(const QString &value);
QJsonObject sanitizeOverlayState(const QJsonObject &state);
bool isAllowedProductionOrigin(const QString &origin);
bool isValidAuthCallbackState(const QString &expected, const QString &received, qint64 expiresAtMs, qint64 nowMs);
QByteArray jsonResponse(const QJsonObject &body);
QString loadSecureSessionToken();
bool storeSecureSessionToken(const QString &token);
void clearSecureSessionToken();

} // namespace draw_stats
