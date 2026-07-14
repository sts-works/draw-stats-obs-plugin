#pragma once

#include <QWidget>

class QCheckBox;
class QButtonGroup;
class QComboBox;
class QLabel;
class QPushButton;
class QStackedWidget;
class QTabWidget;
class QVBoxLayout;

namespace draw_stats {

class ObsStandaloneClient;

class ObsStandaloneDock final : public QWidget {
	Q_OBJECT

public:
	explicit ObsStandaloneDock(ObsStandaloneClient *client, QWidget *parent = nullptr);
	~ObsStandaloneDock() override;

private:
	QWidget *buildSignInPage();
	QWidget *buildAppsPage();
	QWidget *buildPermissionsPage();
	QWidget *buildSettingsPage();
	QWidget *buildStartingPage();
	QWidget *buildDashboardPage();
	QWidget *buildBrowsePage();
	QWidget *buildHeader();
	void buildUi();
	void refresh();
	void refreshSuggestions();
	void refreshRecent();
	void refreshBrowseChooseState();
	void showBrowseDialog();
	void beginStartingTransition();
	void addOverlayToScene();
	QString textFor(const QString &english, const QString &japanese) const;

	ObsStandaloneClient *client_ = nullptr;
	bool japanese_ = false;
	QWidget *content_ = nullptr;
	QStackedWidget *pages_ = nullptr;
	QVBoxLayout *suggestionsLayout_ = nullptr;
	QLabel *scanLabel_ = nullptr;
	QLabel *targetLabel_ = nullptr;
	QLabel *idleValueLabel_ = nullptr;
	QLabel *authStatusLabel_ = nullptr;
	QLabel *statusLabel_ = nullptr;
	QLabel *recordingLabel_ = nullptr;
	QLabel *focusLabel_ = nullptr;
	QLabel *metricsLabel_ = nullptr;
	QLabel *targetNotice_ = nullptr;
	QLabel *permissionStatusLabel_ = nullptr;
	QLabel *dashboardPermissionLabel_ = nullptr;
	QPushButton *recordingButton_ = nullptr;
	QPushButton *appsNextButton_ = nullptr;
	QPushButton *permissionNextButton_ = nullptr;
	QPushButton *browseChooseButton_ = nullptr;
	QPushButton *openTargetButton_ = nullptr;
	QWidget *dashboardPermissionPanel_ = nullptr;
	QVBoxLayout *recentRowsLayout_ = nullptr;
	QButtonGroup *browseAppsGroup_ = nullptr;
	QButtonGroup *browseProcessesGroup_ = nullptr;
	QVBoxLayout *browseAppsLayout_ = nullptr;
	QVBoxLayout *browseProcessesLayout_ = nullptr;
	QTabWidget *browseTabs_ = nullptr;
	QWidget *timelineWidget_ = nullptr;
	int browseReturnPage_ = 1;
};

} // namespace draw_stats
