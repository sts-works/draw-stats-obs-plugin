#include "obs_standalone_client.hpp"
#include "obs_standalone_dock.hpp"
#include "obs_standalone_feature_flag.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>

#include <QDockWidget>
#include <QCoreApplication>
#include <QLabel>
#include <QMainWindow>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("draw-stats-obs-plugin", "en-US")

namespace {

draw_stats::ObsStandaloneClient *client = nullptr;
draw_stats::ObsStandaloneDock *dock = nullptr;
QDockWidget *dockWrapper = nullptr;
bool frontendCallbackActive = false;

QWidget *createDisabledDock()
{
	auto *content = new QWidget;
	content->setStyleSheet(QStringLiteral(
		"QWidget{background:#151517;color:#f7f7f8;}"
		"QLabel#badge{background:#ff861c;color:#111113;font-weight:800;border-radius:6px;padding:5px;}"
		"QLabel#title{font-size:20px;font-weight:750;}"
		"QLabel#copy{color:#a8a8ad;font-size:13px;}"));
	auto *layout = new QVBoxLayout(content);
	layout->setContentsMargins(20, 22, 20, 20);
	layout->setSpacing(12);
	auto *badge = new QLabel(QStringLiteral("DS"));
	badge->setObjectName(QStringLiteral("badge"));
	badge->setFixedSize(34, 30);
	badge->setAlignment(Qt::AlignCenter);
	layout->addWidget(badge, 0, Qt::AlignLeft);
	auto *title = new QLabel(QStringLiteral("Draw Stats is currently unavailable"));
	title->setObjectName(QStringLiteral("title"));
	title->setWordWrap(true);
	layout->addWidget(title);
	auto *copy = new QLabel(QStringLiteral("This feature has been disabled for this release channel."));
	copy->setObjectName(QStringLiteral("copy"));
	copy->setWordWrap(true);
	layout->addWidget(copy);
	layout->addStretch();
	return content;
}

void revealDock()
{
	if (!dockWrapper)
		return;

	auto *mainWindow = static_cast<QMainWindow *>(obs_frontend_get_main_window());
	if (mainWindow) {
		dockWrapper->setFloating(false);
		mainWindow->addDockWidget(Qt::RightDockWidgetArea, dockWrapper);
		dockWrapper->setFloating(false);
		mainWindow->resizeDocks({dockWrapper}, {380}, Qt::Horizontal);
	}
	dockWrapper->setVisible(true);
	dockWrapper->toggleViewAction()->setChecked(true);
	dockWrapper->raise();
	const QRect geometry = dockWrapper->geometry();
	blog(LOG_INFO, "[Draw Stats] dock reveal visible=%d hidden=%d floating=%d area=%d geometry=%d,%d %dx%d",
	     dockWrapper->isVisible(), dockWrapper->isHidden(), dockWrapper->isFloating(),
	     mainWindow ? static_cast<int>(mainWindow->dockWidgetArea(dockWrapper)) : -1, geometry.x(), geometry.y(),
	     geometry.width(), geometry.height());
}

void frontendEvent(enum obs_frontend_event event, void *)
{
	if (event == OBS_FRONTEND_EVENT_EXIT) {
		frontendCallbackActive = false;
		return;
	}
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING)
		revealDock();
}

} // namespace

MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("DrawStats.ModuleDescription");
}

bool obs_module_load(void)
{
	char *qtPluginPath = obs_module_file("qt-plugins");
	if (qtPluginPath) {
		QCoreApplication::addLibraryPath(QString::fromUtf8(qtPluginPath));
		bfree(qtPluginPath);
	}
	QWidget *dockContent = nullptr;
	if (draw_stats::obsStandaloneFeatureEnabled()) {
		client = new draw_stats::ObsStandaloneClient;
		dock = new draw_stats::ObsStandaloneDock(client);
		client->setParent(dock);
		dockContent = dock;
		if (!client->start())
			blog(LOG_WARNING, "[Draw Stats] standalone services started with a recoverable error");
	} else {
		dockContent = createDisabledDock();
		blog(LOG_INFO, "[Draw Stats] standalone client disabled by feature flag");
	}
	dockWrapper = new QDockWidget(QString::fromUtf8(obs_module_text("DrawStats.DockTitle")));
	dockWrapper->setWidget(dockContent);
	dockWrapper->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
	dockWrapper->setMinimumWidth(380);
	if (!obs_frontend_add_custom_qdock("draw-stats-standalone", dockWrapper)) {
		blog(LOG_ERROR, "[Draw Stats] could not add the standalone dock");
		delete dockWrapper;
		dockWrapper = nullptr;
		dock = nullptr;
		client = nullptr;
		return false;
	}
	QObject::connect(dockWrapper, &QObject::destroyed, [] {
		dockWrapper = nullptr;
		dock = nullptr;
		client = nullptr;
	});
	revealDock();
	obs_frontend_add_event_callback(frontendEvent, nullptr);
	frontendCallbackActive = true;
	QTimer::singleShot(1000, dockWrapper, revealDock);
	blog(LOG_INFO, "[Draw Stats] standalone OBS client loaded");
	return true;
}

void obs_module_unload(void)
{
	if (frontendCallbackActive)
		obs_frontend_remove_event_callback(frontendEvent, nullptr);
	frontendCallbackActive = false;
	if (dockWrapper) {
		obs_frontend_remove_dock("draw-stats-standalone");
		delete dockWrapper;
	}
	dockWrapper = nullptr;
	dock = nullptr;
	client = nullptr;
}
