#include "obs_standalone_security.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QUrl>

#ifdef __APPLE__
#include <Security/Security.h>
extern "C" CFTypeRef drawStatsCreateNoninteractiveAuthContext();
#elif defined(_WIN32)
#include <windows.h>
#include <wincred.h>
#endif

namespace draw_stats {

QString normalizeProcessIdentity(const QString &value)
{
	QString normalized = value.trimmed().toLower();
	normalized.replace('\\', '/');
	const qsizetype slash = normalized.lastIndexOf('/');
	if (slash >= 0)
		normalized = normalized.mid(slash + 1);
	for (const QString &suffix : {QStringLiteral(".app"), QStringLiteral(".exe")}) {
		if (normalized.endsWith(suffix)) {
			normalized.chop(suffix.size());
			break;
		}
	}
	return normalized;
}

QString escapeHtml(const QString &value)
{
	return value.toHtmlEscaped();
}

QJsonObject sanitizeOverlayState(const QJsonObject &state)
{
	QJsonObject output;
	static const char *const scalarKeys[] = {
		"recording", "recordingLabel", "elapsedSeconds", "focusSeconds", "targetAvailable",
	};
	for (const char *key : scalarKeys) {
		if (state.contains(key))
			output.insert(key, state.value(key));
	}

	const QJsonObject metrics = state.value("metrics").toObject();
	QJsonObject safeMetrics;
	static const char *const metricKeys[] = {"keyboard", "text", "click", "drag", "wheel"};
	for (const char *key : metricKeys)
		safeMetrics.insert(key, qMax(0, metrics.value(key).toInt()));
	output.insert("metrics", safeMetrics);

	QJsonArray safeTimeline;
	const QJsonArray timeline = state.value("timeline").toArray();
	for (const QJsonValue value : timeline) {
		const QJsonObject bucket = value.toObject();
		QJsonObject safeBucket;
		safeBucket.insert("minute", qMax(0, bucket.value("minute").toInt()));
		for (const char *key : metricKeys)
			safeBucket.insert(key, qMax(0, bucket.value(key).toInt()));
		safeTimeline.append(safeBucket);
		if (safeTimeline.size() == 30)
			break;
	}
	output.insert("timeline", safeTimeline);

	QJsonArray safeRecent;
	const QJsonArray recent = state.value("recent").toArray();
	static const QStringList allowedKinds = {"keyboard", "text", "click", "drag", "wheel"};
	for (const QJsonValue value : recent) {
		const QJsonObject event = value.toObject();
		const QString kind = event.value("kind").toString();
		if (!allowedKinds.contains(kind))
			continue;
		safeRecent.append(QJsonObject{{"kind", kind}, {"at", event.value("at").toDouble()}});
		if (safeRecent.size() == 12)
			break;
	}
	output.insert("recent", safeRecent);

	const QJsonObject visibility = state.value("visibility").toObject();
	output.insert("visibility", QJsonObject{
					    {"recording", visibility.value("recording").toBool(true)},
					    {"metrics", visibility.value("metrics").toBool(true)},
					    {"timeline", visibility.value("timeline").toBool(true)},
					    {"recent", visibility.value("recent").toBool(true)},
				    });
	return output;
}

bool isAllowedProductionOrigin(const QString &origin)
{
	const QUrl url(origin);
	return url.isValid() && url.scheme() == "https" && url.host() == "drawstats.sts.works" && url.port(-1) == -1;
}

bool isValidAuthCallbackState(const QString &expected, const QString &received, qint64 expiresAtMs, qint64 nowMs)
{
	const QByteArray expectedBytes = expected.toUtf8();
	const QByteArray receivedBytes = received.toUtf8();
	if (expectedBytes.isEmpty() || expectedBytes.size() != receivedBytes.size() || nowMs > expiresAtMs)
		return false;
	unsigned char difference = 0;
	for (qsizetype index = 0; index < expectedBytes.size(); ++index)
		difference |= static_cast<unsigned char>(expectedBytes.at(index) ^ receivedBytes.at(index));
	return difference == 0;
}

QByteArray jsonResponse(const QJsonObject &body)
{
	return QJsonDocument(body).toJson(QJsonDocument::Compact);
}

QString loadSecureSessionToken()
{
#ifdef __APPLE__
	const QByteArray service("works.sts.draw-stats.obs-plugin");
	const QByteArray account("production-session");
	CFStringRef serviceRef = CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8 *>(service.constData()),
							 service.size(), kCFStringEncodingUTF8, false);
	CFStringRef accountRef = CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8 *>(account.constData()),
							 account.size(), kCFStringEncodingUTF8, false);
	CFTypeRef authContext = drawStatsCreateNoninteractiveAuthContext();
	const void *keys[] = {kSecClass,      kSecAttrService, kSecAttrAccount,
			      kSecReturnData, kSecMatchLimit,  kSecUseAuthenticationContext};
	const void *values[] = {kSecClassGenericPassword, serviceRef,        accountRef,
				kCFBooleanTrue,           kSecMatchLimitOne, authContext};
	CFDictionaryRef query = CFDictionaryCreate(nullptr, keys, values, 6, &kCFTypeDictionaryKeyCallBacks,
						   &kCFTypeDictionaryValueCallBacks);
	CFTypeRef result = nullptr;
	const OSStatus status = SecItemCopyMatching(query, &result);
	CFRelease(query);
	CFRelease(authContext);
	CFRelease(serviceRef);
	CFRelease(accountRef);
	if (status != errSecSuccess || !result)
		return {};
	CFDataRef data = static_cast<CFDataRef>(result);
	const QString token =
		QString::fromUtf8(reinterpret_cast<const char *>(CFDataGetBytePtr(data)), CFDataGetLength(data));
	CFRelease(result);
	return token;
#elif defined(_WIN32)
	PCREDENTIALW credential = nullptr;
	if (!CredReadW(L"DrawStatsOBS/production-session", CRED_TYPE_GENERIC, 0, &credential))
		return {};
	const QString token = QString::fromUtf8(reinterpret_cast<const char *>(credential->CredentialBlob),
						static_cast<int>(credential->CredentialBlobSize));
	CredFree(credential);
	return token;
#else
	return {};
#endif
}

bool storeSecureSessionToken(const QString &token)
{
	if (token.isEmpty())
		return false;
#ifdef __APPLE__
	clearSecureSessionToken();
	const QByteArray service("works.sts.draw-stats.obs-plugin");
	const QByteArray account("production-session");
	const QByteArray secret = token.toUtf8();
	CFStringRef serviceRef = CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8 *>(service.constData()),
							 service.size(), kCFStringEncodingUTF8, false);
	CFStringRef accountRef = CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8 *>(account.constData()),
							 account.size(), kCFStringEncodingUTF8, false);
	CFDataRef dataRef = CFDataCreate(nullptr, reinterpret_cast<const UInt8 *>(secret.constData()), secret.size());
	const void *keys[] = {kSecClass, kSecAttrService, kSecAttrAccount, kSecValueData};
	const void *values[] = {kSecClassGenericPassword, serviceRef, accountRef, dataRef};
	CFDictionaryRef item = CFDictionaryCreate(nullptr, keys, values, 4, &kCFTypeDictionaryKeyCallBacks,
						  &kCFTypeDictionaryValueCallBacks);
	const OSStatus status = SecItemAdd(item, nullptr);
	CFRelease(item);
	CFRelease(serviceRef);
	CFRelease(accountRef);
	CFRelease(dataRef);
	return status == errSecSuccess;
#elif defined(_WIN32)
	const QByteArray secret = token.toUtf8();
	CREDENTIALW credential{};
	credential.Type = CRED_TYPE_GENERIC;
	credential.TargetName = const_cast<wchar_t *>(L"DrawStatsOBS/production-session");
	credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
	credential.CredentialBlobSize = static_cast<DWORD>(secret.size());
	credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char *>(secret.constData()));
	credential.UserName = const_cast<wchar_t *>(L"Draw Stats");
	return CredWriteW(&credential, 0) == TRUE;
#else
	return false;
#endif
}

void clearSecureSessionToken()
{
#ifdef __APPLE__
	const QByteArray service("works.sts.draw-stats.obs-plugin");
	const QByteArray account("production-session");
	CFStringRef serviceRef = CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8 *>(service.constData()),
							 service.size(), kCFStringEncodingUTF8, false);
	CFStringRef accountRef = CFStringCreateWithBytes(nullptr, reinterpret_cast<const UInt8 *>(account.constData()),
							 account.size(), kCFStringEncodingUTF8, false);
	const void *keys[] = {kSecClass, kSecAttrService, kSecAttrAccount};
	const void *values[] = {kSecClassGenericPassword, serviceRef, accountRef};
	CFDictionaryRef query = CFDictionaryCreate(nullptr, keys, values, 3, &kCFTypeDictionaryKeyCallBacks,
						   &kCFTypeDictionaryValueCallBacks);
	SecItemDelete(query);
	CFRelease(query);
	CFRelease(serviceRef);
	CFRelease(accountRef);
#elif defined(_WIN32)
	CredDeleteW(L"DrawStatsOBS/production-session", CRED_TYPE_GENERIC, 0);
#endif
}

} // namespace draw_stats
