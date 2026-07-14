#include "obs_standalone_feature_flag.hpp"
#include "obs_standalone_security.hpp"
#include "obs_standalone_client.hpp"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>

#include <obs-module.h>

#include <cassert>

#ifndef DRAW_STATS_ENABLE_OBS_QA_MODE
#error "DRAW_STATS_ENABLE_OBS_QA_MODE must be defined explicitly by the build"
#endif

static_assert(DRAW_STATS_ENABLE_OBS_QA_MODE == 0 || DRAW_STATS_ENABLE_OBS_QA_MODE == 1);

extern "C" obs_module_t *obs_current_module(void)
{
	return nullptr;
}

extern "C" char *obs_find_module_file(obs_module_t *, const char *)
{
	return nullptr;
}

extern "C" void bfree(void *) {}

extern "C" void blog(int, const char *, ...) {}

namespace draw_stats {

class ObsStandaloneClientTestAccess {
public:
	static void startProjection(ObsStandaloneClient &client)
	{
		client.recording_ = true;
		client.targetAvailable_ = true;
		client.selectedTargetName_ = QStringLiteral("obs64.exe");
		client.selectedTargetMode_ = QStringLiteral("process");
		client.selectedProcessNames_ = {QStringLiteral("obs64.exe")};
		client.idleMinutes_ = 15;
		client.lastInputAt_ = QDateTime::currentSecsSinceEpoch();
		client.rollTimeline();
	}

	static void acceptHelperMessage(ObsStandaloneClient &client, const QJsonObject &message)
	{
		client.handleHelperMessage(message);
	}

	static void advanceClock(ObsStandaloneClient &client) { client.advanceClock(); }
};

} // namespace draw_stats

int main(int argc, char **argv)
{
	QCoreApplication application(argc, argv);
	const QJsonObject unsafe{{"recording", true},
				 {"session_token", "secret"},
				 {"process_name", "PrivatePainter.exe"},
				 {"metrics", QJsonObject{{"keyboard", 4}, {"click", 2}}},
				 {"recent", QJsonArray{QJsonObject{{"kind", "click"}, {"at", 12}},
						       QJsonObject{{"kind", "raw_key"}, {"key", "X"}}}}};
	const QJsonObject safe = draw_stats::sanitizeOverlayState(unsafe);
	assert(safe.value("recording").toBool());
	assert(safe.value("metrics").toObject().value("keyboard").toInt() == 4);
	assert(safe.value("recent").toArray().size() == 1);
	assert(!safe.contains("session_token"));
	assert(!safe.contains("process_name"));
	assert(draw_stats::isAllowedProductionOrigin("https://drawstats.sts.works"));
	assert(!draw_stats::isAllowedProductionOrigin("http://drawstats.sts.works"));
	assert(!draw_stats::isAllowedProductionOrigin("https://example.com"));
	assert(draw_stats::isValidAuthCallbackState("expected-state", "expected-state", 2000, 1000));
	assert(!draw_stats::isValidAuthCallbackState("expected-state", "wrong-state", 2000, 1000));
	assert(!draw_stats::isValidAuthCallbackState("expected-state", "expected-state", 999, 1000));
	assert(draw_stats::normalizeProcessIdentity("C:\\Program Files\\Krita\\krita.exe") == "krita");
	assert(draw_stats::normalizeProcessIdentity("/Applications/Krita.app") == "krita");

	qunsetenv("DRAW_STATS_ENABLE_OBS_STANDALONE_CLIENT");
	assert(draw_stats::obsStandaloneFeatureEnabled());
	qputenv("DRAW_STATS_ENABLE_OBS_STANDALONE_CLIENT", "false");
	assert(!draw_stats::obsStandaloneFeatureEnabled());
	qputenv("DRAW_STATS_ENABLE_OBS_STANDALONE_CLIENT", "yes");
	assert(draw_stats::obsStandaloneFeatureEnabled());
	qputenv("DRAW_STATS_ENABLE_OBS_STANDALONE_CLIENT", "invalid");
	assert(draw_stats::obsStandaloneFeatureEnabled());
	qunsetenv("DRAW_STATS_ENABLE_OBS_STANDALONE_CLIENT");

	draw_stats::ObsStandaloneClient client;
	draw_stats::ObsStandaloneClientTestAccess::startProjection(client);
	const QJsonObject matchingInput{{"type", "input"},
					{"kind", "click"},
					{"app", "obs64.exe"},
					{"process", "obs64.exe"},
					{"bundle", ""}};
	draw_stats::ObsStandaloneClientTestAccess::acceptHelperMessage(client, matchingInput);
	QJsonObject dashboard = client.dashboardState();
	assert(dashboard.value("metrics").toObject().value("click").toInteger() == 1);
	assert(dashboard.value("timeline").toArray().last().toObject().value("click").toInteger() == 1);
	assert(dashboard.value("recent").toArray().first().toObject().value("kind").toString() == "click");

	const QJsonObject unrelatedInput{{"type", "input"},
					 {"kind", "keyboard"},
					 {"app", "node.exe"},
					 {"process", "node.exe"},
					 {"bundle", ""}};
	draw_stats::ObsStandaloneClientTestAccess::acceptHelperMessage(client, unrelatedInput);
	dashboard = client.dashboardState();
	assert(dashboard.value("metrics").toObject().value("keyboard").toInteger() == 0);
	assert(dashboard.value("metrics").toObject().value("click").toInteger() == 1);
	draw_stats::ObsStandaloneClientTestAccess::advanceClock(client);
	assert(client.dashboardState().value("focusSeconds").toInteger() == 1);
	return 0;
}
