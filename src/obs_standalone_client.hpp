#pragma once

#include "obs_standalone_overlay_server.hpp"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QProcess>
#include <QSettings>
#include <QTimer>

#include <array>
#include <functional>

namespace draw_stats {

struct DrawingApp {
	QString id;
	QString name;
	QStringList processNames;
	QStringList keywords;
	int priority = 0;
};

struct RunningApp {
	QString name;
	QString process;
	QString bundle;
	QString path;
	bool running = false;
};

class ObsStandaloneClient final : public QObject {
	Q_OBJECT

public:
	explicit ObsStandaloneClient(QObject *parent = nullptr);
	~ObsStandaloneClient() override;

	bool start();
	void shutdown();

	bool isSignedIn() const;
	bool isRecording() const;
	bool isScanning() const;
	bool onboardingComplete() const;
	bool targetAvailable() const;
	bool inputCaptureReady() const;
	int idleMinutes() const;
	QString statusText() const;
	QString selectedTargetName() const;
	QString selectedTargetMode() const;
	QString helperPermission() const;
	QString overlayUrl() const;
	QList<DrawingApp> catalog() const;
	QList<DrawingApp> suggestions() const;
	QList<RunningApp> runningApps() const;
	QJsonObject dashboardState() const;

	void beginSignIn(const QString &provider);
	void signOut();
	void selectCatalogApp(const DrawingApp &app);
	void selectRunningProcess(const RunningApp &app);
	void setIdleMinutes(int minutes);
	void setOnboardingComplete(bool complete);
	bool startRecording();
	void stopRecording();
	bool openSelectedTarget();
	bool openInputMonitoringSettings();
	void restartInputHelper();
	bool openPlatformStore();
	void setOverlayVisibility(const QString &key, bool visible);
	bool overlayVisible(const QString &key) const;

signals:
	void stateChanged();
	void signInFailed(const QString &message);
	void signInSucceeded();
	void scanCompleted();

private:
	friend class ObsStandaloneClientTestAccess;

	struct Counters {
		qint64 keyboard = 0;
		qint64 text = 0;
		qint64 click = 0;
		qint64 drag = 0;
		qint64 wheel = 0;
		bool empty() const { return keyboard + text + click + drag + wheel == 0; }
		void clear() { keyboard = text = click = drag = wheel = 0; }
	};

	struct MinuteBucket {
		qint64 minute = 0;
		Counters counts;
	};

	void loadSettings();
	void saveSettings();
	void loadFallbackCatalog();
	void fetchCatalog();
	void applyCatalogJson(const QByteArray &data);
	void startHelper();
	void consumeHelperOutput();
	void handleHelperMessage(const QJsonObject &message);
	void updateSuggestions();
	bool eventMatchesTarget(const QJsonObject &message) const;
	void recordInput(const QString &kind);
	void advanceClock();
	void rollTimeline();
	void flushCaptureBatch();
	void postRuntime(const QJsonObject &body, std::function<void(int, const QJsonObject &)> callback = {});
	void acceptSessionToken(const QString &token);
	void setStatus(const QString &status);
	QString localHostName() const;

	QSettings settings_;
	ObsStandaloneOverlayServer overlayServer_;
	QNetworkAccessManager network_;
	QProcess helper_;
	QByteArray helperBuffer_;
	QTimer clockTimer_;
	QTimer ingestTimer_;

	QList<DrawingApp> catalog_;
	QList<DrawingApp> suggestions_;
	QList<RunningApp> runningApps_;
	QString sessionToken_;
	QString statusText_;
	QString helperPermission_ = QStringLiteral("unknown");
	QString selectedTargetName_;
	QString selectedTargetMode_;
	QStringList selectedProcessNames_;
	QString selectedLaunchPath_;
	bool scanning_ = true;
	bool targetAvailable_ = false;
	bool onboardingComplete_ = false;
	bool recording_ = false;
	bool shuttingDown_ = false;
	bool qaVisualMode_ = false;
	bool qaOfflineMode_ = false;
	int idleMinutes_ = 15;
	qint64 recordingStartedAt_ = 0;
	qint64 lastInputAt_ = 0;
	qint64 focusSeconds_ = 0;
	qint64 sequence_ = 0;
	QString artworkId_;
	QString captureSessionId_;
	Counters total_;
	Counters pending_;
	QList<MinuteBucket> timeline_;
	QJsonArray recent_;
	QJsonObject visibility_{{"recording", true}, {"metrics", true}, {"timeline", true}, {"recent", true}};
};

} // namespace draw_stats
