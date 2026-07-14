#include "obs_standalone_client.hpp"

#include "obs_standalone_security.hpp"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QHostInfo>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSysInfo>
#include <QUrlQuery>
#include <QUuid>

#include <obs-module.h>

namespace draw_stats {

namespace {

constexpr auto kWebsiteOrigin = "https://drawstats.sts.works";
constexpr auto kRuntimeEndpoint = "https://drawstats.sts.works/api/desktop/runtime";
constexpr auto kCatalogEndpoint =
	"https://drawstats.sts.works/config/flutter_desktop_shell_windows_drawing_app_catalog.v1.json";

qint64 currentMinute()
{
	return QDateTime::currentSecsSinceEpoch() / 60;
}

QString displayKind(const QString &kind)
{
	if (kind == "text")
		return QStringLiteral("Text input");
	return kind.left(1).toUpper() + kind.mid(1);
}

} // namespace

ObsStandaloneClient::ObsStandaloneClient(QObject *parent)
	: QObject(parent),
	  settings_(QStringLiteral("STS Works"), QStringLiteral("Draw Stats OBS")),
	  overlayServer_([this] { return dashboardState(); }, this)
{
	connect(&overlayServer_, &ObsStandaloneOverlayServer::authTokenReceived, this,
		&ObsStandaloneClient::acceptSessionToken);
	connect(&overlayServer_, &ObsStandaloneOverlayServer::authFailed, this,
		[this](const QString &reason) { emit signInFailed(reason); });
	connect(&helper_, &QProcess::readyReadStandardOutput, this, &ObsStandaloneClient::consumeHelperOutput);
	connect(&helper_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
		scanning_ = false;
		helperPermission_ = QStringLiteral("unavailable");
		setStatus(QStringLiteral("Input helper is unavailable."));
		emit scanCompleted();
	});

	clockTimer_.setInterval(1000);
	connect(&clockTimer_, &QTimer::timeout, this, &ObsStandaloneClient::advanceClock);
	ingestTimer_.setInterval(5000);
	connect(&ingestTimer_, &QTimer::timeout, this, &ObsStandaloneClient::flushCaptureBatch);
}

ObsStandaloneClient::~ObsStandaloneClient()
{
	shutdown();
}

bool ObsStandaloneClient::start()
{
	loadSettings();
	loadFallbackCatalog();
	sessionToken_ = loadSecureSessionToken();
#if DRAW_STATS_ENABLE_OBS_QA_MODE
	qaVisualMode_ = settings_.value("qaVisualMode", false).toBool();
	qaOfflineMode_ = settings_.value("qaOfflineMode", false).toBool();
	if (sessionToken_.isEmpty() && (qaVisualMode_ || qaOfflineMode_))
		sessionToken_ = QStringLiteral("qa-local-session-only");
#endif
	if (!overlayServer_.start()) {
		setStatus(QStringLiteral("Local overlay could not start."));
		return false;
	}
	startHelper();
	fetchCatalog();
	rollTimeline();
	clockTimer_.start();
	ingestTimer_.start();
	setStatus(isSignedIn() ? QStringLiteral("Ready") : QStringLiteral("Sign in to start recording."));
	return true;
}

void ObsStandaloneClient::shutdown()
{
	if (shuttingDown_)
		return;
	shuttingDown_ = true;
	if (recording_)
		stopRecording();
	flushCaptureBatch();
	ingestTimer_.stop();
	clockTimer_.stop();
	overlayServer_.stop();
	if (helper_.state() != QProcess::NotRunning) {
		helper_.terminate();
		if (!helper_.waitForFinished(1200)) {
			helper_.kill();
			helper_.waitForFinished(500);
		}
	}
}

bool ObsStandaloneClient::isSignedIn() const
{
	return !sessionToken_.isEmpty();
}
bool ObsStandaloneClient::isRecording() const
{
	return recording_;
}
bool ObsStandaloneClient::isScanning() const
{
	return scanning_;
}
bool ObsStandaloneClient::onboardingComplete() const
{
	return onboardingComplete_;
}
bool ObsStandaloneClient::targetAvailable() const
{
	return targetAvailable_;
}
bool ObsStandaloneClient::inputCaptureReady() const
{
	return helperPermission_ == QStringLiteral("allowed");
}
int ObsStandaloneClient::idleMinutes() const
{
	return idleMinutes_;
}
QString ObsStandaloneClient::statusText() const
{
	return statusText_;
}
QString ObsStandaloneClient::selectedTargetName() const
{
	return selectedTargetName_;
}
QString ObsStandaloneClient::selectedTargetMode() const
{
	return selectedTargetMode_;
}
QString ObsStandaloneClient::helperPermission() const
{
	return helperPermission_;
}
QString ObsStandaloneClient::overlayUrl() const
{
	return overlayServer_.overlayUrl();
}
QList<DrawingApp> ObsStandaloneClient::catalog() const
{
	return catalog_;
}
QList<DrawingApp> ObsStandaloneClient::suggestions() const
{
	return suggestions_;
}
QList<RunningApp> ObsStandaloneClient::runningApps() const
{
	return runningApps_;
}

void ObsStandaloneClient::beginSignIn(const QString &provider)
{
	static const QStringList allowed = {"google", "apple", "microsoft"};
	if (!allowed.contains(provider)) {
		emit signInFailed(QStringLiteral("Unsupported sign-in provider."));
		return;
	}
	if (!overlayServer_.port() && !overlayServer_.start()) {
		emit signInFailed(QStringLiteral("The local sign-in callback could not start."));
		return;
	}

	const QString callback = overlayServer_.beginAuthCallbackUrl();
	const QString returnTo = QStringLiteral("/desktop-auth/callback?desktop_callback=%1")
					 .arg(QString::fromUtf8(QUrl::toPercentEncoding(callback)));
	QUrl url(QStringLiteral("%1/api/auth/%2-auth").arg(QString::fromUtf8(kWebsiteOrigin), provider));
	QUrlQuery query;
	query.addQueryItem(QStringLiteral("return_to"), returnTo);
	url.setQuery(query);
	if (!QDesktopServices::openUrl(url)) {
		emit signInFailed(QStringLiteral("The system browser could not be opened."));
		return;
	}
	setStatus(QStringLiteral("Complete sign-in in your browser."));
}

void ObsStandaloneClient::acceptSessionToken(const QString &token)
{
	if (token.size() < 16) {
		emit signInFailed(QStringLiteral("The returned session was invalid."));
		return;
	}
	sessionToken_ = token;
	if (!storeSecureSessionToken(token))
		blog(LOG_WARNING, "[Draw Stats] session is active but secure persistence failed");
	setStatus(QStringLiteral("Signed in. Choose what you draw with."));
	emit signInSucceeded();
}

void ObsStandaloneClient::signOut()
{
	if (recording_)
		stopRecording();
	sessionToken_.clear();
	clearSecureSessionToken();
	artworkId_.clear();
	captureSessionId_.clear();
	setStatus(QStringLiteral("Signed out."));
}

void ObsStandaloneClient::selectCatalogApp(const DrawingApp &app)
{
	selectedTargetMode_ = QStringLiteral("app");
	selectedTargetName_ = app.name;
	selectedProcessNames_ = app.processNames;
	for (const QString &keyword : app.keywords)
		selectedProcessNames_.append(keyword);
	selectedProcessNames_.append(app.name);
	selectedLaunchPath_.clear();
	for (const RunningApp &candidate : runningApps_) {
		const QString haystack = (candidate.name + " " + candidate.process + " " + candidate.bundle).toLower();
		bool matched = false;
		for (const QString &identity : selectedProcessNames_) {
			QString needle = normalizeProcessIdentity(identity);
			if (needle.endsWith(".exe"))
				needle.chop(4);
			if (!needle.isEmpty() && haystack.contains(needle)) {
				matched = true;
				break;
			}
		}
		if (matched && !candidate.path.isEmpty()) {
			selectedLaunchPath_ = candidate.path;
			break;
		}
	}
	saveSettings();
	updateSuggestions();
	if (recording_)
		setStatus(targetAvailable_ ? QStringLiteral("Recording")
					   : QStringLiteral("Recording is ready. Open the selected app to begin."));
}

void ObsStandaloneClient::selectRunningProcess(const RunningApp &app)
{
	selectedTargetMode_ = QStringLiteral("process");
	selectedTargetName_ = app.name.isEmpty() ? app.process : app.name;
	selectedProcessNames_ = {app.process};
	selectedLaunchPath_ = app.path;
	saveSettings();
	updateSuggestions();
	if (recording_)
		setStatus(targetAvailable_ ? QStringLiteral("Recording")
					   : QStringLiteral("Recording is ready. Open the selected app to begin."));
}

void ObsStandaloneClient::setIdleMinutes(int minutes)
{
	idleMinutes_ = qBound(0, minutes, 720);
	saveSettings();
	emit stateChanged();
}

void ObsStandaloneClient::setOnboardingComplete(bool complete)
{
	onboardingComplete_ = complete;
	saveSettings();
	emit stateChanged();
}

bool ObsStandaloneClient::startRecording()
{
	if (recording_)
		return true;
	if (!isSignedIn()) {
		setStatus(QStringLiteral("Sign in before recording."));
		return false;
	}
	if (selectedTargetName_.isEmpty()) {
		setStatus(QStringLiteral("Choose a drawing app or process first."));
		return false;
	}
	if (!inputCaptureReady()) {
		setStatus(QStringLiteral("Input Monitoring is required before recording."));
		return false;
	}

	recording_ = true;
	recordingStartedAt_ = QDateTime::currentSecsSinceEpoch();
	lastInputAt_ = recordingStartedAt_;
	captureSessionId_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
	sequence_ = 0;
	setStatus(targetAvailable_ ? QStringLiteral("Recording")
				   : QStringLiteral("Recording is ready. Open the selected app to begin."));

	const QJsonObject body{{"action", "start_recording"},
			       {"host_name", localHostName()},
			       {"artwork_title", ""},
			       {"auto_progress_screenshot_enabled", false}};
	postRuntime(body, [this](int status, const QJsonObject &response) {
		if (status >= 200 && status < 300) {
			artworkId_ = response.value("current_artwork_id").toString();
			focusSeconds_ = qMax<qint64>(focusSeconds_,
						     response.value("recording_elapsed_seconds_base").toInteger());
			setStatus(targetAvailable_ ? QStringLiteral("Recording")
						   : QStringLiteral("Open the selected app to begin capturing."));
		} else if (status == 401 || status == 403) {
			recording_ = false;
			signOut();
			emit signInFailed(QStringLiteral("Your session expired. Sign in again."));
		} else {
			setStatus(QStringLiteral("Recording locally. Cloud sync will retry."));
		}
	});
	return true;
}

void ObsStandaloneClient::stopRecording()
{
	if (!recording_)
		return;
	flushCaptureBatch();
	recording_ = false;
	const QJsonObject body{{"action", "stop_recording"},
			       {"host_name", localHostName()},
			       {"artwork_id", artworkId_},
			       {"capture_session_id", captureSessionId_}};
	postRuntime(body);
	setStatus(QStringLiteral("Recording stopped."));
}

bool ObsStandaloneClient::openSelectedTarget()
{
	if (!selectedLaunchPath_.isEmpty())
		return QDesktopServices::openUrl(QUrl::fromLocalFile(selectedLaunchPath_));
#ifdef __APPLE__
	if (!selectedTargetName_.isEmpty())
		return QProcess::startDetached(QStringLiteral("/usr/bin/open"),
					       {QStringLiteral("-a"), selectedTargetName_});
#endif
	return false;
}

bool ObsStandaloneClient::openInputMonitoringSettings()
{
#ifdef __APPLE__
	return QProcess::startDetached(
		QStringLiteral("/usr/bin/open"),
		{QStringLiteral("x-apple.systempreferences:com.apple.preference.security?Privacy_ListenEvent")});
#else
	return false;
#endif
}

void ObsStandaloneClient::restartInputHelper()
{
	if (helper_.state() != QProcess::NotRunning) {
		helper_.terminate();
		if (!helper_.waitForFinished(1200)) {
			helper_.kill();
			helper_.waitForFinished(500);
		}
	}
	helperBuffer_.clear();
	helperPermission_ = QStringLiteral("unknown");
	setStatus(QStringLiteral("Checking input access…"));
	startHelper();
}

bool ObsStandaloneClient::openPlatformStore()
{
#ifdef __APPLE__
	return QDesktopServices::openUrl(
		QUrl(QStringLiteral("https://apps.apple.com/us/app/draw-stats/id6771253274?mt=12")));
#elif defined(_WIN32)
	if (QDesktopServices::openUrl(QUrl(QStringLiteral("ms-windows-store://pdp/?ProductId=9NDP6M5K7HH8"))))
		return true;
	return QDesktopServices::openUrl(QUrl(QStringLiteral("https://apps.microsoft.com/detail/9NDP6M5K7HH8")));
#else
	return QDesktopServices::openUrl(QUrl(QStringLiteral("https://drawstats.sts.works")));
#endif
}

void ObsStandaloneClient::setOverlayVisibility(const QString &key, bool visible)
{
	static const QStringList allowed = {"recording", "metrics", "timeline", "recent"};
	if (!allowed.contains(key))
		return;
	visibility_.insert(key, visible);
	saveSettings();
	emit stateChanged();
}

bool ObsStandaloneClient::overlayVisible(const QString &key) const
{
	return visibility_.value(key).toBool(true);
}

void ObsStandaloneClient::loadSettings()
{
	idleMinutes_ = qBound(0, settings_.value("idleMinutes", 15).toInt(), 720);
	onboardingComplete_ = settings_.value("onboardingComplete", false).toBool();
	selectedTargetName_ = settings_.value("target/name").toString();
	selectedTargetMode_ = settings_.value("target/mode").toString();
	const QJsonDocument processNamesDocument =
		QJsonDocument::fromJson(settings_.value("target/processNamesJson", "[]").toString().toUtf8());
	selectedProcessNames_.clear();
	for (const QJsonValue value : processNamesDocument.array())
		selectedProcessNames_.append(value.toString());
	selectedLaunchPath_ = settings_.value("target/launchPath").toString();
	for (const QString &key : {"recording", "metrics", "timeline", "recent"})
		visibility_.insert(key, settings_.value("overlay/" + key, true).toBool());
}

void ObsStandaloneClient::saveSettings()
{
	settings_.setValue("idleMinutes", idleMinutes_);
	settings_.setValue("onboardingComplete", onboardingComplete_);
	settings_.setValue("target/name", selectedTargetName_);
	settings_.setValue("target/mode", selectedTargetMode_);
	settings_.setValue("target/processNamesJson",
			   QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(selectedProcessNames_))
						     .toJson(QJsonDocument::Compact)));
	settings_.setValue("target/launchPath", selectedLaunchPath_);
	for (const QString &key : {"recording", "metrics", "timeline", "recent"})
		settings_.setValue("overlay/" + key, visibility_.value(key).toBool(true));
	settings_.sync();
}

void ObsStandaloneClient::loadFallbackCatalog()
{
	catalog_ = {
		{"clip_studio_paint",
		 "CLIP STUDIO PAINT",
		 {"clipstudiopaint.exe", "clip studio paint"},
		 {"clip studio", "celsys"},
		 100},
		{"adobe_photoshop", "Adobe Photoshop", {"photoshop.exe", "adobe photoshop"}, {"photoshop"}, 96},
		{"krita", "Krita", {"krita.exe", "krita"}, {"krita"}, 94},
		{"paint_tool_sai", "PaintTool SAI", {"sai.exe", "sai2.exe"}, {"painttool sai"}, 92},
		{"medibang_paint", "MediBang Paint", {"medibangpaintpro.exe", "medibangpaint.exe"}, {"medibang"}, 90},
		{"firealpaca", "FireAlpaca", {"firealpaca.exe"}, {"firealpaca"}, 89},
		{"corel_painter", "Corel Painter", {"painter.exe", "corelpainter.exe"}, {"corel painter"}, 86},
		{"rebelle", "Rebelle", {"rebelle.exe", "rebelle7.exe"}, {"rebelle"}, 85},
		{"artweaver", "Artweaver", {"artweaver.exe"}, {"artweaver"}, 82},
		{"artrage", "ArtRage", {"artrage.exe", "artragevitae.exe"}, {"artrage"}, 82},
		{"open_canvas", "openCanvas", {"opencanvas.exe"}, {"opencanvas"}, 81},
		{"sketchbook", "Sketchbook", {"sketchbook.exe"}, {"sketchbook"}, 80},
	};
}

void ObsStandaloneClient::fetchCatalog()
{
	QNetworkRequest request(QUrl(QString::fromUtf8(kCatalogEndpoint)));
	request.setRawHeader("Accept", "application/json");
	QNetworkReply *reply = network_.get(request);
	connect(reply, &QNetworkReply::finished, this, [this, reply] {
		if (reply->error() == QNetworkReply::NoError)
			applyCatalogJson(reply->readAll());
		reply->deleteLater();
	});
}

void ObsStandaloneClient::applyCatalogJson(const QByteArray &data)
{
	const QJsonDocument document = QJsonDocument::fromJson(data);
	const QJsonArray entries = document.object().value("entries").toArray();
	QList<DrawingApp> parsed;
	for (const QJsonValue value : entries) {
		const QJsonObject object = value.toObject();
		DrawingApp app;
		app.id = object.value("id").toString();
		app.name = object.value("displayName").toString();
		app.priority = object.value("priority").toInt();
		for (const QJsonValue item : object.value("processNames").toArray())
			app.processNames.append(item.toString());
		for (const QJsonValue item : object.value("keywords").toArray())
			app.keywords.append(item.toString());
		if (!app.id.isEmpty() && !app.name.isEmpty())
			parsed.append(app);
	}
	if (!parsed.isEmpty()) {
		std::sort(parsed.begin(), parsed.end(),
			  [](const DrawingApp &a, const DrawingApp &b) { return a.priority > b.priority; });
		catalog_ = parsed;
		updateSuggestions();
	}
}

void ObsStandaloneClient::startHelper()
{
#ifdef _WIN32
	constexpr auto helperName = "bin/draw-stats-obs-input-helper.exe";
#else
	constexpr auto helperName = "bin/draw-stats-obs-input-helper";
#endif
	char *path = obs_module_file(helperName);
	if (!path) {
		scanning_ = false;
		helperPermission_ = QStringLiteral("unavailable");
		setStatus(QStringLiteral("Input helper is missing."));
		return;
	}
	const QString helperPath = QString::fromUtf8(path);
	bfree(path);
	helper_.setProgram(helperPath);
	helper_.setArguments({});
	helper_.setProcessChannelMode(QProcess::SeparateChannels);
	helper_.start();
}

void ObsStandaloneClient::consumeHelperOutput()
{
	helperBuffer_ += helper_.readAllStandardOutput();
	for (;;) {
		const qsizetype newline = helperBuffer_.indexOf('\n');
		if (newline < 0)
			break;
		const QByteArray line = helperBuffer_.left(newline).trimmed();
		helperBuffer_.remove(0, newline + 1);
		const QJsonDocument document = QJsonDocument::fromJson(line);
		if (document.isObject())
			handleHelperMessage(document.object());
	}
}

void ObsStandaloneClient::handleHelperMessage(const QJsonObject &message)
{
	const QString type = message.value("type").toString();
	if (type == "permission") {
		helperPermission_ = message.value("state").toString();
		if (!inputCaptureReady() && recording_)
			stopRecording();
		if (inputCaptureReady())
			setStatus(recording_ ? QStringLiteral("Recording") : QStringLiteral("Input capture is ready."));
		else
			setStatus(QStringLiteral("Input Monitoring is required. Operations are not being counted."));
		return;
	}
	if (type == "apps") {
		QList<RunningApp> next;
		for (const QJsonValue value : message.value("items").toArray()) {
			const QJsonObject object = value.toObject();
			RunningApp app{object.value("name").toString(), object.value("process").toString(),
				       object.value("bundle").toString(), object.value("path").toString(),
				       object.value("running").toBool(true)};
			if (!app.process.isEmpty() || !app.name.isEmpty())
				next.append(app);
		}
		runningApps_ = next;
		const bool firstScan = scanning_;
		scanning_ = false;
		updateSuggestions();
		if (firstScan)
			emit scanCompleted();
		return;
	}
	if (type == "input" && recording_ && eventMatchesTarget(message))
		recordInput(message.value("kind").toString());
}

void ObsStandaloneClient::updateSuggestions()
{
	suggestions_.clear();
	for (const DrawingApp &candidate : catalog_) {
		bool matched = false;
		QStringList identities = candidate.processNames + candidate.keywords + QStringList{candidate.name};
		for (const RunningApp &running : runningApps_) {
			const QString haystack =
				(running.name + " " + running.process + " " + running.bundle).toLower();
			for (const QString &identity : identities) {
				QString needle = normalizeProcessIdentity(identity);
				if (needle.endsWith(".exe"))
					needle.chop(4);
				if (!needle.isEmpty() && haystack.contains(needle)) {
					matched = true;
					break;
				}
			}
			if (matched)
				break;
		}
		if (matched)
			suggestions_.append(candidate);
		if (suggestions_.size() == 4)
			break;
	}

	targetAvailable_ = false;
	for (const RunningApp &app : runningApps_) {
		if (!app.running)
			continue;
		QJsonObject identity{{"app", app.name}, {"process", app.process}, {"bundle", app.bundle}};
		if (eventMatchesTarget(identity)) {
			targetAvailable_ = true;
			break;
		}
	}
#if DRAW_STATS_ENABLE_OBS_QA_MODE
	if (qaVisualMode_ && !selectedTargetName_.isEmpty())
		targetAvailable_ = true;
#endif
	emit stateChanged();
}

bool ObsStandaloneClient::eventMatchesTarget(const QJsonObject &message) const
{
	if (selectedTargetName_.isEmpty())
		return false;
	const QString process = normalizeProcessIdentity(message.value("process").toString());
	const QString app = normalizeProcessIdentity(message.value("app").toString());
	const QString bundle = normalizeProcessIdentity(message.value("bundle").toString());
	const QString haystack = process + " " + app + " " + bundle;
	for (QString identity : selectedProcessNames_) {
		identity = normalizeProcessIdentity(identity);
		if (identity.endsWith(".exe"))
			identity.chop(4);
		if (identity.isEmpty())
			continue;
		if (selectedTargetMode_ == "process" ? process == identity : haystack.contains(identity))
			return true;
	}
	return false;
}

void ObsStandaloneClient::recordInput(const QString &kind)
{
	Counters *counts[] = {&total_, &pending_};
	for (Counters *counter : counts) {
		if (kind == "keyboard")
			counter->keyboard++;
		else if (kind == "text")
			counter->text++;
		else if (kind == "click")
			counter->click++;
		else if (kind == "drag")
			counter->drag++;
		else if (kind == "wheel")
			counter->wheel++;
		else
			return;
	}

	lastInputAt_ = QDateTime::currentSecsSinceEpoch();
	rollTimeline();
	MinuteBucket &bucket = timeline_.last();
	if (kind == "keyboard")
		bucket.counts.keyboard++;
	else if (kind == "text")
		bucket.counts.text++;
	else if (kind == "click")
		bucket.counts.click++;
	else if (kind == "drag")
		bucket.counts.drag++;
	else if (kind == "wheel")
		bucket.counts.wheel++;

	recent_.prepend(
		QJsonObject{{"kind", kind}, {"label", displayKind(kind)}, {"at", QDateTime::currentMSecsSinceEpoch()}});
	while (recent_.size() > 12)
		recent_.removeLast();
	emit stateChanged();
}

void ObsStandaloneClient::advanceClock()
{
	rollTimeline();
#if DRAW_STATS_ENABLE_OBS_QA_MODE
	if (qaVisualMode_ && recording_) {
		static const QStringList qaKinds = {"keyboard", "text", "click", "drag", "wheel"};
		recordInput(qaKinds.at(static_cast<int>(QDateTime::currentSecsSinceEpoch() % qaKinds.size())));
	}
#endif
	if (recording_ && targetAvailable_) {
		const qint64 now = QDateTime::currentSecsSinceEpoch();
		if (idleMinutes_ == 0 || now - lastInputAt_ <= static_cast<qint64>(idleMinutes_) * 60)
			focusSeconds_++;
	}
	emit stateChanged();
}

void ObsStandaloneClient::rollTimeline()
{
	const qint64 minute = currentMinute();
	if (timeline_.isEmpty() || timeline_.last().minute != minute)
		timeline_.append(MinuteBucket{minute, {}});
	while (timeline_.size() > 30)
		timeline_.removeFirst();
}

void ObsStandaloneClient::flushCaptureBatch()
{
	if (!recording_ || !isSignedIn())
		return;
	if (artworkId_.isEmpty()) {
		const QJsonObject startBody{{"action", "start_recording"},
					    {"host_name", localHostName()},
					    {"artwork_title", ""},
					    {"auto_progress_screenshot_enabled", false}};
		postRuntime(startBody, [this](int status, const QJsonObject &response) {
			if (status >= 200 && status < 300) {
				artworkId_ = response.value("current_artwork_id").toString();
				setStatus(QStringLiteral("Recording"));
			}
		});
		return;
	}
	if (pending_.empty() || captureSessionId_.isEmpty())
		return;
	const Counters batch = pending_;
	pending_.clear();
	const qint64 sequence = ++sequence_;
	QJsonObject body{{"action", "ingest_capture_batch"},
			 {"host_name", localHostName()},
			 {"artwork_id", artworkId_},
			 {"capture_session_id", captureSessionId_},
			 {"capture_sequence_no", sequence},
			 {"captured_at", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
			 {"keyboard_count", batch.keyboard},
			 {"text_input_count", batch.text},
			 {"click_count", batch.click},
			 {"drag_count", batch.drag},
			 {"wheel_count", batch.wheel},
			 {"active_seconds", focusSeconds_},
			 {"timeline_elapsed_seconds", focusSeconds_}};
	postRuntime(body, [this, batch](int status, const QJsonObject &) {
		if (status < 200 || status >= 300) {
			pending_.keyboard += batch.keyboard;
			pending_.text += batch.text;
			pending_.click += batch.click;
			pending_.drag += batch.drag;
			pending_.wheel += batch.wheel;
			setStatus(QStringLiteral("Recording locally. Cloud sync will retry."));
		} else if (recording_) {
			setStatus(targetAvailable_ ? QStringLiteral("Recording")
						   : QStringLiteral("Open the selected app to continue."));
		}
	});
}

void ObsStandaloneClient::postRuntime(const QJsonObject &body, std::function<void(int, const QJsonObject &)> callback)
{
#if DRAW_STATS_ENABLE_OBS_QA_MODE
	if (qaVisualMode_ || qaOfflineMode_) {
		const QString action = body.value("action").toString();
		QJsonObject response{{"status", action == "stop_recording" ? "stopped" : "recording"}};
		if (action == "start_recording") {
			response.insert("current_artwork_id", "qa-visual-artwork");
			response.insert("recording_elapsed_seconds_base", 0);
		}
		QTimer::singleShot(40, this, [callback = std::move(callback), response] {
			if (callback)
				callback(200, response);
		});
		return;
	}
#endif
	if (!isSignedIn()) {
		if (callback)
			callback(401, {});
		return;
	}
	QNetworkRequest request(QUrl(QString::fromUtf8(kRuntimeEndpoint)));
	request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
	request.setRawHeader("Accept", "application/json");
	request.setRawHeader("Origin", kWebsiteOrigin);
	request.setRawHeader("Cookie", QByteArray("draw_stats_auth_session=") + sessionToken_.toUtf8());
	QNetworkReply *reply = network_.post(request, QJsonDocument(body).toJson(QJsonDocument::Compact));
	connect(reply, &QNetworkReply::finished, this, [reply, callback = std::move(callback)] {
		const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
		const QJsonDocument document = QJsonDocument::fromJson(reply->readAll());
		if (callback)
			callback(status, document.object());
		reply->deleteLater();
	});
}

QJsonObject ObsStandaloneClient::dashboardState() const
{
	QJsonArray timeline;
	const qint64 now = currentMinute();
	for (qint64 minute = now - 29; minute <= now; ++minute) {
		Counters counts;
		for (const MinuteBucket &bucket : timeline_) {
			if (bucket.minute == minute) {
				counts = bucket.counts;
				break;
			}
		}
		timeline.append(QJsonObject{{"minute", minute},
					    {"keyboard", counts.keyboard},
					    {"text", counts.text},
					    {"click", counts.click},
					    {"drag", counts.drag},
					    {"wheel", counts.wheel}});
	}
	return QJsonObject{{"recording", recording_},
			   {"recordingLabel", recording_ ? "Recording" : "Paused"},
			   {"elapsedSeconds",
			    recording_ ? QDateTime::currentSecsSinceEpoch() - recordingStartedAt_ : 0},
			   {"focusSeconds", focusSeconds_},
			   {"targetAvailable", targetAvailable_},
			   {"metrics", QJsonObject{{"keyboard", total_.keyboard},
						   {"text", total_.text},
						   {"click", total_.click},
						   {"drag", total_.drag},
						   {"wheel", total_.wheel}}},
			   {"timeline", timeline},
			   {"recent", recent_},
			   {"visibility", visibility_}};
}

void ObsStandaloneClient::setStatus(const QString &status)
{
	if (statusText_ == status)
		return;
	statusText_ = status;
	emit stateChanged();
}

QString ObsStandaloneClient::localHostName() const
{
	const QString host = QHostInfo::localHostName().trimmed();
	return host.isEmpty() ? QSysInfo::machineHostName() : host;
}

} // namespace draw_stats
