#include "obs_standalone_dock.hpp"

#include "obs_standalone_client.hpp"

#include <QCheckBox>
#include <QButtonGroup>
#include <QAbstractButton>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QStackedWidget>
#include <QStyle>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include <obs-frontend-api.h>
#include <obs-module.h>

namespace draw_stats {

namespace {

QString formatDuration(qint64 seconds)
{
	const qint64 hours = seconds / 3600;
	const qint64 minutes = (seconds % 3600) / 60;
	const qint64 remaining = seconds % 60;
	if (hours > 0)
		return QStringLiteral("%1h %2m").arg(hours).arg(minutes, 2, 10, QLatin1Char('0'));
	return QStringLiteral("%1:%2").arg(minutes, 2, 10, QLatin1Char('0')).arg(remaining, 2, 10, QLatin1Char('0'));
}

QString idleLabel(int minutes, bool japanese)
{
	if (minutes == 0)
		return japanese ? QStringLiteral("なし") : QStringLiteral("None");
	if (minutes < 60)
		return japanese ? QStringLiteral("%1分").arg(minutes) : QStringLiteral("%1 min").arg(minutes);
	const int hours = minutes / 60;
	const int remainder = minutes % 60;
	if (remainder == 0)
		return japanese ? QStringLiteral("%1時間").arg(hours) : QStringLiteral("%1 hr").arg(hours);
	return japanese ? QStringLiteral("%1時間 %2分").arg(hours).arg(remainder)
			: QStringLiteral("%1 hr %2 min").arg(hours).arg(remainder);
}

QIcon activityIcon(const QString &kind)
{
	QPixmap pixmap(24, 24);
	pixmap.fill(Qt::transparent);
	QPainter painter(&pixmap);
	painter.setRenderHint(QPainter::Antialiasing);
	const QColor accent = kind == QStringLiteral("text")    ? QColor("#41b9ff")
			      : kind == QStringLiteral("click") ? QColor("#ffc52a")
			      : kind == QStringLiteral("drag")  ? QColor("#d76dff")
			      : kind == QStringLiteral("wheel") ? QColor("#59d88b")
								: QColor("#ff8a1f");
	painter.setPen(QPen(accent, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	painter.setBrush(Qt::NoBrush);
	if (kind == QStringLiteral("keyboard")) {
		painter.drawRoundedRect(QRectF(3.5, 6.5, 17, 11), 2.5, 2.5);
		for (int x : {7, 11, 15, 18})
			painter.drawPoint(QPointF(x, 10.5));
		painter.drawLine(QPointF(7, 14), QPointF(17, 14));
	} else if (kind == QStringLiteral("text")) {
		QFont font = painter.font();
		font.setBold(true);
		font.setPixelSize(17);
		painter.setFont(font);
		painter.drawText(pixmap.rect(), Qt::AlignCenter, QStringLiteral("T"));
	} else if (kind == QStringLiteral("drag")) {
		painter.drawLine(QPointF(5, 12), QPointF(19, 12));
		painter.drawLine(QPointF(5, 12), QPointF(9, 8));
		painter.drawLine(QPointF(5, 12), QPointF(9, 16));
		painter.drawLine(QPointF(19, 12), QPointF(15, 8));
		painter.drawLine(QPointF(19, 12), QPointF(15, 16));
	} else {
		painter.drawRoundedRect(QRectF(7, 3.5, 10, 17), 5, 5);
		painter.drawLine(QPointF(12, 4), QPointF(12, 9));
		if (kind == QStringLiteral("click")) {
			painter.drawLine(QPointF(18.5, 4.5), QPointF(21, 2));
			painter.drawLine(QPointF(19.5, 8), QPointF(22, 8));
		} else {
			painter.drawEllipse(QPointF(12, 8), 1.1, 1.8);
		}
	}
	return QIcon(pixmap);
}

class RecordingShapeWidget final : public QWidget {
public:
	RecordingShapeWidget(ObsStandaloneClient *client, bool japanese, QWidget *parent = nullptr)
		: QWidget(parent),
		  client_(client),
		  japanese_(japanese)
	{
		setMinimumHeight(194);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing);
		const QRectF panel = rect().adjusted(1, 1, -1, -1);
		painter.setPen(QPen(QColor("#343438"), 1));
		painter.setBrush(QColor("#18181b"));
		painter.drawRoundedRect(panel, 7, 7);

		painter.setPen(QColor("#f4f4f5"));
		QFont heading = painter.font();
		heading.setWeight(QFont::DemiBold);
		painter.setFont(heading);
		painter.drawText(QRectF(18, 14, width() - 36, 22),
				 japanese_ ? QStringLiteral("記録の動作イメージ") : QStringLiteral("Recording shape"));

		painter.setFont(QFont(painter.font().family(), 10));
		painter.setPen(QColor("#a9a9b0"));
		painter.drawText(
			QRectF(18, 40, width() - 36, 38), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap,
			japanese_ ? QStringLiteral("短い休止は作業時間に含め、しきい値を超えた時間だけ除外します。")
				  : QStringLiteral(
					    "Short pauses count as work. Only time beyond the threshold is excluded."));

		const qreal left = 20;
		const qreal right = width() - 20;
		const qreal y = 128;
		const qreal activeEnd = left + (right - left) * 0.28;
		const qreal idleStart =
			client_->idleMinutes() == 0
				? right
				: left + (right - left) * (0.46 + 0.20 * client_->idleMinutes() / 720.0);
		const qreal resumeStart = left + (right - left) * 0.80;

		auto segment = [&](qreal x1, qreal x2, const QColor &first, const QColor &last, bool glow) {
			if (x2 <= x1)
				return;
			QLinearGradient gradient(x1, y, x2, y);
			gradient.setColorAt(0, first);
			gradient.setColorAt(1, last);
			if (glow) {
				painter.setPen(QPen(QColor(first.red(), first.green(), first.blue(), 42), 18,
						    Qt::SolidLine, Qt::RoundCap));
				painter.drawLine(QPointF(x1, y), QPointF(x2, y));
			}
			painter.setPen(QPen(QBrush(gradient), 10, Qt::SolidLine, Qt::RoundCap));
			painter.drawLine(QPointF(x1, y), QPointF(x2, y));
		};
		segment(left, activeEnd, QColor("#ff7a18"), QColor("#ff9b32"), true);
		segment(activeEnd + 3, qMin(idleStart, resumeStart) - 3, QColor("#ffc52a"), QColor("#ffad22"), true);
		if (client_->idleMinutes() > 0)
			segment(idleStart + 3, resumeStart - 3, QColor("#55565a"), QColor("#74757a"), false);
		segment(resumeStart + 3, right, QColor("#ff8a1f"), QColor("#ffb048"), true);

		painter.setPen(QPen(QColor("#2f3034"), 1, Qt::DashLine));
		painter.drawLine(QPointF(idleStart, 84), QPointF(idleStart, 150));
		painter.setPen(QColor("#d8d8dc"));
		painter.drawText(QRectF(idleStart - 55, 82, 110, 18), Qt::AlignCenter,
				 idleLabel(client_->idleMinutes(), japanese_));

		painter.setPen(QColor("#888991"));
		painter.drawText(QRectF(left, 159, width() * 0.28, 18),
				 japanese_ ? QStringLiteral("作業中") : QStringLiteral("Working"));
		painter.drawText(QRectF(activeEnd, 159, width() * 0.32, 18), Qt::AlignCenter,
				 japanese_ ? QStringLiteral("短い休止（計測）")
					   : QStringLiteral("Short break (counted)"));
		painter.drawText(QRectF(resumeStart, 159, right - resumeStart, 18), Qt::AlignRight,
				 japanese_ ? QStringLiteral("作業に復帰") : QStringLiteral("Back to work"));
	}

private:
	ObsStandaloneClient *client_;
	bool japanese_;
};

class DashboardTimelineWidget final : public QWidget {
public:
	explicit DashboardTimelineWidget(ObsStandaloneClient *client, QWidget *parent = nullptr)
		: QWidget(parent),
		  client_(client)
	{
		setMinimumHeight(122);
		setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
	}

protected:
	void paintEvent(QPaintEvent *) override
	{
		QPainter painter(this);
		painter.setRenderHint(QPainter::Antialiasing);
		painter.setPen(QPen(QColor("#303036"), 1));
		painter.setBrush(QColor("#18181b"));
		painter.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 7, 7);
		painter.setPen(QColor("#f2f2f3"));
		QFont font = painter.font();
		font.setWeight(QFont::DemiBold);
		painter.setFont(font);
		painter.drawText(QRectF(14, 10, width() - 28, 20), QStringLiteral("Live timeline"));

		const QJsonArray buckets = client_->dashboardState().value("timeline").toArray();
		int maximum = 1;
		for (const QJsonValue value : buckets) {
			const QJsonObject bucket = value.toObject();
			maximum = qMax(maximum, bucket.value("keyboard").toInt() + bucket.value("text").toInt() +
							bucket.value("click").toInt() + bucket.value("drag").toInt() +
							bucket.value("wheel").toInt());
		}
		const qreal left = 14;
		const qreal top = 38;
		const qreal graphHeight = 55;
		const qreal gap = 1;
		const qreal barWidth = qMax<qreal>(2, (width() - 28 - gap * 29) / 30.0);
		QLinearGradient graphSurface(left, top, width() - 14, top);
		graphSurface.setColorAt(0, QColor(255, 138, 31, 8));
		graphSurface.setColorAt(0.65, QColor(65, 185, 255, 7));
		graphSurface.setColorAt(1, QColor(255, 138, 31, 14));
		painter.setPen(Qt::NoPen);
		painter.setBrush(graphSurface);
		painter.drawRoundedRect(QRectF(left, top, width() - 28, graphHeight), 4, 4);
		painter.setPen(QPen(QColor("#29292e"), 1));
		for (int marker = 0; marker <= 6; ++marker) {
			const qreal x = left + (width() - 28) * marker / 6.0;
			painter.drawLine(QPointF(x, top), QPointF(x, top + graphHeight));
		}
		const QColor colors[] = {QColor("#ff8a1f"), QColor("#41b9ff"), QColor("#ffc52a"), QColor("#d76dff"),
					 QColor("#59d88b")};
		const char *keys[] = {"keyboard", "text", "click", "drag", "wheel"};
		for (int index = 0; index < buckets.size() && index < 30; ++index) {
			const QJsonObject bucket = buckets.at(index).toObject();
			const qreal x = left + index * (barWidth + gap);
			qreal bottom = top + graphHeight;
			painter.setPen(Qt::NoPen);
			painter.setBrush(QColor("#242429"));
			painter.drawRoundedRect(QRectF(x, bottom - 2, barWidth, 2), 1, 1);
			for (int kind = 0; kind < 5; ++kind) {
				const int value = bucket.value(keys[kind]).toInt();
				if (value <= 0)
					continue;
				const qreal height = qMax<qreal>(2, graphHeight * value / maximum);
				QLinearGradient gradient(x, bottom - height, x + barWidth, bottom);
				gradient.setColorAt(0, colors[kind].lighter(125));
				gradient.setColorAt(1, colors[kind]);
				painter.setPen(Qt::NoPen);
				painter.setBrush(gradient);
				painter.drawRoundedRect(QRectF(x, bottom - height, barWidth, height), 2, 2);
				bottom -= height;
			}
			if (index == buckets.size() - 1) {
				painter.setPen(QPen(QColor(255, 154, 46, 190), 1.2));
				painter.setBrush(Qt::NoBrush);
				painter.drawRoundedRect(QRectF(x - 1, top - 2, barWidth + 2, graphHeight + 4), 3, 3);
			}
		}
		painter.setPen(QColor("#777880"));
		painter.setFont(QFont(painter.font().family(), 9));
		painter.drawText(QRectF(left, 98, 70, 16), QStringLiteral("-30m"));
		painter.drawText(QRectF(width() - 64, 98, 50, 16), Qt::AlignRight, QStringLiteral("Now"));
	}

private:
	ObsStandaloneClient *client_;
};

QLabel *sectionTitle(const QString &text)
{
	auto *label = new QLabel(text);
	label->setObjectName(QStringLiteral("sectionTitle"));
	return label;
}

QFrame *divider()
{
	auto *line = new QFrame;
	line->setFrameShape(QFrame::HLine);
	line->setObjectName(QStringLiteral("divider"));
	return line;
}

} // namespace

ObsStandaloneDock::ObsStandaloneDock(ObsStandaloneClient *client, QWidget *parent) : QWidget(parent), client_(client)
{
	setObjectName(QStringLiteral("drawStatsDock"));
	setMinimumWidth(380);
	setMinimumHeight(660);
	japanese_ = QSettings(QStringLiteral("STS Works"), QStringLiteral("Draw Stats OBS"))
			    .value("language", "en")
			    .toString() == "ja";
	auto *shell = new QVBoxLayout(this);
	shell->setContentsMargins(0, 0, 0, 0);
	buildUi();
	connect(client_, &ObsStandaloneClient::stateChanged, this, &ObsStandaloneDock::refresh);
	connect(client_, &ObsStandaloneClient::signInSucceeded, this, &ObsStandaloneDock::refresh);
	connect(client_, &ObsStandaloneClient::signInFailed, this, [this](const QString &message) {
		if (authStatusLabel_)
			authStatusLabel_->setText(message);
	});
	refresh();
}

ObsStandaloneDock::~ObsStandaloneDock()
{
	if (client_) {
		disconnect(client_, nullptr, this, nullptr);
		client_->shutdown();
	}
}

QString ObsStandaloneDock::textFor(const QString &english, const QString &japanese) const
{
	return japanese_ ? japanese : english;
}

void ObsStandaloneDock::buildUi()
{
	if (content_) {
		layout()->removeWidget(content_);
		content_->deleteLater();
	}
	content_ = new QWidget(this);
	auto *root = new QVBoxLayout(content_);
	root->setContentsMargins(14, 12, 14, 14);
	root->setSpacing(10);
	root->addWidget(buildHeader());
	pages_ = new QStackedWidget;
	pages_->addWidget(buildSignInPage());
	pages_->addWidget(buildAppsPage());
	pages_->addWidget(buildPermissionsPage());
	pages_->addWidget(buildSettingsPage());
	pages_->addWidget(buildStartingPage());
	pages_->addWidget(buildDashboardPage());
	pages_->addWidget(buildBrowsePage());
	root->addWidget(pages_, 1);
	layout()->addWidget(content_);

	setStyleSheet(QStringLiteral(R"QSS(
    QWidget#drawStatsDock, QWidget { background: #151517; color: #f6f6f7; font-size: 13px; }
    QLabel#brand { font-size: 18px; font-weight: 700; color: #ffffff; }
    QLabel#eyebrow { color: #ff9b32; font-size: 11px; font-weight: 700; }
    QLabel#pageTitle { font-size: 22px; font-weight: 700; }
    QLabel#muted { color: #a5a5ac; }
    QLabel#sectionTitle { font-size: 14px; font-weight: 700; }
    QLabel#status { color: #c6c6cc; background: #1c1c1f; border: 1px solid #303034; padding: 8px 10px; border-radius: 6px; }
    QFrame#divider { color: #2d2d31; }
    QFrame#panel { background: #1f1f21; border: 1px solid #35353a; border-radius: 7px; }
    QFrame#warningPanel { background: #2a2017; border: 1px solid #8a571f; border-radius: 7px; }
    QPushButton { background: #242428; border: 1px solid #3b3b40; border-radius: 6px; min-height: 34px; padding: 0 12px; font-weight: 600; }
    QPushButton:hover { background: #2d2d32; border-color: #55555c; }
    QPushButton:pressed { background: #1b1b1e; }
    QPushButton[primary="true"] { background: #ff8a1f; border-color: #ff9b32; color: #16130f; }
    QPushButton[primary="true"]:hover { background: #ff9b32; }
    QPushButton[recording="true"] { background: #2a1d15; border-color: #ff8a1f; color: #ffad5d; }
    QPushButton[browseOption="true"] { text-align: left; min-height: 36px; background: #1d1d21; border-color: #303035; }
    QPushButton[browseOption="true"]:checked { background: #3a281a; border-color: #ff8a1f; color: #ffb054; }
    QPushButton:disabled { color: #65656b; background: #1c1c1f; border-color: #29292d; }
    QComboBox, QLineEdit { background: #202023; border: 1px solid #3b3b40; border-radius: 6px; min-height: 32px; padding: 0 10px; }
    QComboBox::drop-down { border: 0; width: 24px; }
    QSlider::groove:horizontal { height: 5px; background: #343439; border-radius: 2px; }
    QSlider::sub-page:horizontal { background: #ff8a1f; border-radius: 2px; }
    QSlider::handle:horizontal { width: 16px; margin: -6px 0; border-radius: 8px; background: #fff; border: 2px solid #ff8a1f; }
    QCheckBox { spacing: 8px; }
    QCheckBox::indicator { width: 17px; height: 17px; border: 1px solid #55555c; border-radius: 4px; background: #202023; }
    QCheckBox::indicator:checked { background: #ff8a1f; border-color: #ff9b32; }
    QListWidget { background: #18181b; border: 1px solid #303034; border-radius: 6px; outline: none; }
    QListWidget::item { min-height: 32px; padding: 4px 8px; border-bottom: 1px solid #242428; }
    QListWidget::item:selected { background: #30251d; color: #fff; }
    QTabWidget::pane { border: 1px solid #303034; border-radius: 6px; top: -1px; background: #18181b; }
    QTabBar::tab { background: #232327; border: 1px solid #36363b; padding: 8px 13px; margin-right: 3px; border-top-left-radius: 5px; border-top-right-radius: 5px; }
    QTabBar::tab:selected { background: #3a281a; border-color: #ff8a1f; color: #ffb054; }
    QProgressBar { background: #28282d; border: 0; border-radius: 2px; height: 4px; }
    QProgressBar::chunk { background: #ff8a1f; border-radius: 2px; }
    QScrollArea { background: #18181b; border: 0; }
  )QSS"));
	refresh();
}

QWidget *ObsStandaloneDock::buildHeader()
{
	auto *header = new QWidget;
	auto *layout = new QHBoxLayout(header);
	layout->setContentsMargins(2, 0, 2, 0);
	auto *mark = new QLabel(QStringLiteral("DS"));
	mark->setFixedSize(30, 30);
	mark->setAlignment(Qt::AlignCenter);
	mark->setStyleSheet(QStringLiteral("background:#ff8a1f;color:#16130f;border-radius:6px;font-weight:800;"));
	auto *brand = new QLabel(QStringLiteral("Draw Stats"));
	brand->setObjectName(QStringLiteral("brand"));
	layout->addWidget(mark);
	layout->addWidget(brand);
	layout->addStretch();
	auto *language = new QComboBox;
	language->addItems({QStringLiteral("English"), QStringLiteral("日本語")});
	language->setCurrentIndex(japanese_ ? 1 : 0);
	language->setFixedWidth(92);
	connect(language, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int index) {
		const bool nextJapanese = index == 1;
		if (nextJapanese == japanese_)
			return;
		japanese_ = nextJapanese;
		QSettings(QStringLiteral("STS Works"), QStringLiteral("Draw Stats OBS"))
			.setValue("language", japanese_ ? "ja" : "en");
		QTimer::singleShot(0, this, &ObsStandaloneDock::buildUi);
	});
	layout->addWidget(language);
	return header;
}

QWidget *ObsStandaloneDock::buildSignInPage()
{
	auto *page = new QWidget;
	auto *layout = new QVBoxLayout(page);
	layout->setContentsMargins(4, 18, 4, 4);
	layout->setSpacing(12);
	auto *eyebrow = new QLabel(textFor("STANDALONE CAPTURE", "OBS独立キャプチャ"));
	eyebrow->setObjectName(QStringLiteral("eyebrow"));
	auto *title = new QLabel(textFor("Create with proof, live.", "制作の証拠を、リアルタイムに。"));
	title->setObjectName(QStringLiteral("pageTitle"));
	title->setWordWrap(true);
	auto *copy = new QLabel(
		textFor("Capture drawing activity, see it immediately, and place the live dashboard on your stream.",
			"描画操作をローカルで即時集計し、ライブダッシュボードをそのまま配信に重ねられます。"));
	copy->setObjectName(QStringLiteral("muted"));
	copy->setWordWrap(true);
	layout->addWidget(eyebrow);
	layout->addWidget(title);
	layout->addWidget(copy);
	layout->addSpacing(12);
	for (const auto &provider : {qMakePair(QStringLiteral("google"), QStringLiteral("Google")),
				     qMakePair(QStringLiteral("apple"), QStringLiteral("Apple")),
				     qMakePair(QStringLiteral("microsoft"), QStringLiteral("Microsoft"))}) {
		auto *button = new QPushButton(textFor(QStringLiteral("Continue with %1").arg(provider.second),
						       QStringLiteral("%1で続ける").arg(provider.second)));
		if (provider.first == "google")
			button->setProperty("primary", true);
		connect(button, &QPushButton::clicked, this,
			[this, provider] { client_->beginSignIn(provider.first); });
		layout->addWidget(button);
	}
	layout->addStretch();
	authStatusLabel_ = new QLabel(client_->statusText());
	authStatusLabel_->setObjectName(QStringLiteral("status"));
	authStatusLabel_->setWordWrap(true);
	layout->addWidget(authStatusLabel_);
	return page;
}

QWidget *ObsStandaloneDock::buildAppsPage()
{
	auto *page = new QWidget;
	auto *layout = new QVBoxLayout(page);
	layout->setContentsMargins(4, 8, 4, 4);
	layout->setSpacing(9);
	auto *step = new QLabel(textFor("STEP 1 OF 3  ·  APPS", "ステップ 1 / 3  ·  アプリ"));
	step->setObjectName(QStringLiteral("eyebrow"));
	auto *title = new QLabel(textFor("What do you draw with?", "描画に使うアプリを選択"));
	title->setObjectName(QStringLiteral("pageTitle"));
	auto *copy = new QLabel(textFor("We only count input while the selected app is active.",
					"選択したアプリがアクティブな間だけ操作を集計します。"));
	copy->setObjectName(QStringLiteral("muted"));
	copy->setWordWrap(true);
	layout->addWidget(step);
	layout->addWidget(title);
	layout->addWidget(copy);
	scanLabel_ = new QLabel;
	scanLabel_->setObjectName(QStringLiteral("status"));
	layout->addWidget(scanLabel_);
	auto *suggestions = new QWidget;
	suggestionsLayout_ = new QVBoxLayout(suggestions);
	suggestionsLayout_->setContentsMargins(0, 0, 0, 0);
	suggestionsLayout_->setSpacing(7);
	layout->addWidget(suggestions);
	auto *browse = new QPushButton(textFor("Browse apps", "アプリを参照"));
	connect(browse, &QPushButton::clicked, this, &ObsStandaloneDock::showBrowseDialog);
	layout->addWidget(browse);
	layout->addStretch();
	targetLabel_ = new QLabel;
	targetLabel_->setObjectName(QStringLiteral("muted"));
	targetLabel_->setWordWrap(true);
	layout->addWidget(targetLabel_);
	appsNextButton_ = new QPushButton(textFor("Next", "次へ"));
	appsNextButton_->setProperty("primary", true);
	connect(appsNextButton_, &QPushButton::clicked, this, [this] { pages_->setCurrentIndex(2); });
	layout->addWidget(appsNextButton_);
	refreshSuggestions();
	return page;
}

QWidget *ObsStandaloneDock::buildPermissionsPage()
{
	auto *page = new QWidget;
	auto *layout = new QVBoxLayout(page);
	layout->setContentsMargins(4, 8, 4, 4);
	layout->setSpacing(10);
	auto *step = new QLabel(textFor("STEP 2 OF 3  ·  PERMISSIONS", "ステップ 2 / 3  ·  権限"));
	step->setObjectName(QStringLiteral("eyebrow"));
	auto *title = new QLabel(textFor("Enable input capture", "入力の記録を許可"));
	title->setObjectName(QStringLiteral("pageTitle"));
	auto *copy = new QLabel(textFor(
		"Draw Stats counts only input categories while your selected app is active. It never stores keys, text, or pointer positions.",
		"選択したアプリがアクティブな間だけ操作の種類を集計します。キー、入力内容、ポインター位置は保存しません。"));
	copy->setObjectName(QStringLiteral("muted"));
	copy->setWordWrap(true);
	layout->addWidget(step);
	layout->addWidget(title);
	layout->addWidget(copy);

	auto *panel = new QFrame;
	panel->setObjectName(QStringLiteral("panel"));
	auto *panelLayout = new QVBoxLayout(panel);
	auto *panelTitle = new QLabel(textFor("Input Monitoring", "入力監視"));
	panelTitle->setObjectName(QStringLiteral("sectionTitle"));
	permissionStatusLabel_ = new QLabel;
	permissionStatusLabel_->setObjectName(QStringLiteral("status"));
	permissionStatusLabel_->setWordWrap(true);
	panelLayout->addWidget(panelTitle);
	panelLayout->addWidget(permissionStatusLabel_);
	auto *permissionActions = new QHBoxLayout;
#ifdef __APPLE__
	auto *openSettings = new QPushButton(textFor("Open settings", "設定を開く"));
	connect(openSettings, &QPushButton::clicked, this, [this] { client_->openInputMonitoringSettings(); });
	permissionActions->addWidget(openSettings, 1);
#endif
	auto *retry = new QPushButton(textFor("Check again", "再確認"));
	connect(retry, &QPushButton::clicked, client_, &ObsStandaloneClient::restartInputHelper);
	permissionActions->addWidget(retry, 1);
	panelLayout->addLayout(permissionActions);
	layout->addWidget(panel);
	layout->addStretch();

	auto *actions = new QHBoxLayout;
	auto *back = new QPushButton(textFor("Back", "戻る"));
	permissionNextButton_ = new QPushButton(textFor("Next", "次へ"));
	permissionNextButton_->setProperty("primary", true);
	connect(back, &QPushButton::clicked, this, [this] { pages_->setCurrentIndex(1); });
	connect(permissionNextButton_, &QPushButton::clicked, this, [this] { pages_->setCurrentIndex(3); });
	actions->addWidget(back);
	actions->addWidget(permissionNextButton_, 1);
	layout->addLayout(actions);
	return page;
}

QWidget *ObsStandaloneDock::buildSettingsPage()
{
	auto *page = new QWidget;
	auto *layout = new QVBoxLayout(page);
	layout->setContentsMargins(4, 8, 4, 4);
	layout->setSpacing(9);
	auto *step = new QLabel(textFor("STEP 3 OF 3  ·  SETTINGS", "ステップ 3 / 3  ·  設定"));
	step->setObjectName(QStringLiteral("eyebrow"));
	auto *title = new QLabel(textFor("Set recording preferences", "記録の設定"));
	title->setObjectName(QStringLiteral("pageTitle"));
	auto *copy = new QLabel(textFor("Set when quiet time should stop counting as work.",
					"操作のない時間を作業時間から除外するタイミングを設定します。"));
	copy->setObjectName(QStringLiteral("muted"));
	copy->setWordWrap(true);
	layout->addWidget(step);
	layout->addWidget(title);
	layout->addWidget(copy);

	auto *row = new QFrame;
	row->setObjectName(QStringLiteral("panel"));
	auto *rowLayout = new QVBoxLayout(row);
	auto *labelRow = new QHBoxLayout;
	auto *idleTitle = new QLabel(textFor("Idle detection time", "アイドル判定時間"));
	idleTitle->setObjectName(QStringLiteral("sectionTitle"));
	idleValueLabel_ = new QLabel(idleLabel(client_->idleMinutes(), japanese_));
	idleValueLabel_->setStyleSheet(QStringLiteral("color:#ffad5d;font-weight:700;"));
	labelRow->addWidget(idleTitle);
	labelRow->addStretch();
	labelRow->addWidget(idleValueLabel_);
	auto *slider = new QSlider(Qt::Horizontal);
	slider->setRange(0, 720);
	slider->setSingleStep(1);
	slider->setPageStep(15);
	slider->setValue(client_->idleMinutes());
	connect(slider, &QSlider::valueChanged, this, [this](int value) {
		client_->setIdleMinutes(value);
		if (idleValueLabel_)
			idleValueLabel_->setText(idleLabel(value, japanese_));
	});
	auto *range = new QHBoxLayout;
	auto *none = new QLabel(textFor("None", "なし"));
	auto *max = new QLabel(textFor("12 hours", "12時間"));
	none->setObjectName(QStringLiteral("muted"));
	max->setObjectName(QStringLiteral("muted"));
	range->addWidget(none);
	range->addStretch();
	range->addWidget(max);
	rowLayout->addLayout(labelRow);
	rowLayout->addWidget(slider);
	rowLayout->addLayout(range);
	layout->addWidget(row);
	auto *shape = new RecordingShapeWidget(client_, japanese_);
	connect(client_, &ObsStandaloneClient::stateChanged, shape, qOverload<>(&QWidget::update));
	layout->addWidget(shape);
	layout->addStretch();
	auto *actions = new QHBoxLayout;
	auto *back = new QPushButton(textFor("Back", "戻る"));
	auto *next = new QPushButton(textFor("Start recording", "記録を開始"));
	next->setProperty("primary", true);
	connect(back, &QPushButton::clicked, this, [this] { pages_->setCurrentIndex(2); });
	connect(next, &QPushButton::clicked, this, &ObsStandaloneDock::beginStartingTransition);
	actions->addWidget(back);
	actions->addWidget(next, 1);
	layout->addLayout(actions);
	return page;
}

QWidget *ObsStandaloneDock::buildStartingPage()
{
	auto *page = new QWidget;
	auto *layout = new QVBoxLayout(page);
	layout->setContentsMargins(12, 24, 12, 24);
	layout->setAlignment(Qt::AlignCenter);
	auto *ring = new QProgressBar;
	ring->setRange(0, 0);
	ring->setFixedWidth(260);
	auto *title = new QLabel(textFor("Recording is starting", "記録を開始しています"));
	title->setObjectName(QStringLiteral("pageTitle"));
	title->setAlignment(Qt::AlignCenter);
	auto *copy = new QLabel(textFor("Connecting the input helper, local timeline, and stream overlay.",
					"入力、ローカルタイムライン、配信オーバーレイを接続しています。"));
	copy->setObjectName(QStringLiteral("muted"));
	copy->setWordWrap(true);
	copy->setAlignment(Qt::AlignCenter);
	layout->addStretch();
	layout->addWidget(ring, 0, Qt::AlignCenter);
	layout->addSpacing(18);
	layout->addWidget(title);
	layout->addWidget(copy);
	layout->addStretch();
	return page;
}

QWidget *ObsStandaloneDock::buildDashboardPage()
{
	auto *page = new QWidget;
	auto *layout = new QVBoxLayout(page);
	layout->setContentsMargins(4, 4, 4, 4);
	layout->setSpacing(8);
	auto *top = new QHBoxLayout;
	auto *heading = new QVBoxLayout;
	auto *eyebrow = new QLabel(textFor("LIVE CAPTURE", "ライブキャプチャ"));
	eyebrow->setObjectName(QStringLiteral("eyebrow"));
	auto *title = new QLabel(textFor("Dashboard", "ダッシュボード"));
	title->setObjectName(QStringLiteral("pageTitle"));
	heading->addWidget(eyebrow);
	heading->addWidget(title);
	recordingButton_ = new QPushButton;
	connect(recordingButton_, &QPushButton::clicked, this, [this] {
		if (client_->isRecording()) {
			client_->stopRecording();
			return;
		}
		client_->startRecording();
	});
	top->addLayout(heading);
	top->addStretch();
	top->addWidget(recordingButton_);
	layout->addLayout(top);

	dashboardPermissionPanel_ = new QFrame;
	dashboardPermissionPanel_->setObjectName(QStringLiteral("warningPanel"));
	auto *permissionLayout = new QVBoxLayout(dashboardPermissionPanel_);
	permissionLayout->setContentsMargins(10, 9, 10, 9);
	permissionLayout->setSpacing(7);
	dashboardPermissionLabel_ = new QLabel;
	dashboardPermissionLabel_->setWordWrap(true);
	permissionLayout->addWidget(dashboardPermissionLabel_);
	auto *permissionActions = new QHBoxLayout;
#ifdef __APPLE__
	auto *openPermissionSettings = new QPushButton(textFor("Open Input Monitoring", "入力監視を開く"));
	connect(openPermissionSettings, &QPushButton::clicked, this,
		[this] { client_->openInputMonitoringSettings(); });
	permissionActions->addWidget(openPermissionSettings, 1);
#endif
	auto *retryPermission = new QPushButton(textFor("Check again", "再確認"));
	connect(retryPermission, &QPushButton::clicked, client_, &ObsStandaloneClient::restartInputHelper);
	permissionActions->addWidget(retryPermission, 1);
	permissionLayout->addLayout(permissionActions);
	layout->addWidget(dashboardPermissionPanel_);

	targetNotice_ = new QLabel;
	targetNotice_->setObjectName(QStringLiteral("status"));
	targetNotice_->setWordWrap(true);
	openTargetButton_ = new QPushButton(textFor("Open app", "アプリを開く"));
	connect(openTargetButton_, &QPushButton::clicked, this, [this] { client_->openSelectedTarget(); });
	auto *changeTargetButton = new QPushButton(textFor("Change", "変更"));
	connect(changeTargetButton, &QPushButton::clicked, this, &ObsStandaloneDock::showBrowseDialog);
	auto *noticeRow = new QHBoxLayout;
	noticeRow->addWidget(targetNotice_, 1);
	noticeRow->addWidget(openTargetButton_);
	noticeRow->addWidget(changeTargetButton);
	layout->addLayout(noticeRow);

	auto *summary = new QFrame;
	summary->setObjectName(QStringLiteral("panel"));
	auto *summaryLayout = new QHBoxLayout(summary);
	auto *recordingBox = new QVBoxLayout;
	auto *recordingCaption = new QLabel(textFor("SESSION", "セッション"));
	recordingCaption->setObjectName(QStringLiteral("eyebrow"));
	recordingLabel_ = new QLabel;
	recordingLabel_->setStyleSheet(QStringLiteral("font-size:20px;font-weight:700;"));
	recordingBox->addWidget(recordingCaption);
	recordingBox->addWidget(recordingLabel_);
	auto *focusBox = new QVBoxLayout;
	auto *focusCaption = new QLabel(textFor("FOCUS TIME", "作業時間"));
	focusCaption->setObjectName(QStringLiteral("eyebrow"));
	focusLabel_ = new QLabel;
	focusLabel_->setStyleSheet(QStringLiteral("font-size:20px;font-weight:700;"));
	focusBox->addWidget(focusCaption);
	focusBox->addWidget(focusLabel_);
	summaryLayout->addLayout(recordingBox, 1);
	summaryLayout->addWidget(divider());
	summaryLayout->addLayout(focusBox, 1);
	layout->addWidget(summary);

	metricsLabel_ = new QLabel;
	metricsLabel_->setObjectName(QStringLiteral("status"));
	metricsLabel_->setWordWrap(true);
	layout->addWidget(metricsLabel_);
	timelineWidget_ = new DashboardTimelineWidget(client_);
	layout->addWidget(timelineWidget_);

	layout->addWidget(sectionTitle(textFor("Recent activity", "最近のアクティビティ")));
	auto *recentPanel = new QFrame;
	recentPanel->setObjectName(QStringLiteral("panel"));
	recentPanel->setMinimumHeight(90);
	recentPanel->setMaximumHeight(116);
	recentRowsLayout_ = new QVBoxLayout(recentPanel);
	recentRowsLayout_->setContentsMargins(10, 7, 10, 7);
	recentRowsLayout_->setSpacing(2);
	layout->addWidget(recentPanel);

	auto *overlayPanel = new QFrame;
	overlayPanel->setObjectName(QStringLiteral("panel"));
	auto *overlayColumn = new QVBoxLayout(overlayPanel);
	overlayColumn->setContentsMargins(10, 8, 10, 10);
	overlayColumn->setSpacing(6);
	overlayColumn->addWidget(sectionTitle(textFor("On stream", "配信に表示")));
	auto *overlayOptions = new QGridLayout;
	overlayOptions->setHorizontalSpacing(12);
	overlayOptions->setVerticalSpacing(4);
	int overlayIndex = 0;
	for (const auto &option : {qMakePair(QStringLiteral("recording"), textFor("Recording", "記録状態")),
				   qMakePair(QStringLiteral("metrics"), textFor("Metrics", "操作数")),
				   qMakePair(QStringLiteral("timeline"), textFor("Timeline", "タイムライン")),
				   qMakePair(QStringLiteral("recent"), textFor("Recent", "最近の操作"))}) {
		auto *check = new QCheckBox(option.second);
		check->setChecked(client_->overlayVisible(option.first));
		connect(check, &QCheckBox::toggled, this,
			[this, key = option.first](bool checked) { client_->setOverlayVisibility(key, checked); });
		overlayOptions->addWidget(check, overlayIndex / 2, overlayIndex % 2);
		++overlayIndex;
	}
	overlayColumn->addLayout(overlayOptions);
	auto *add = new QPushButton(textFor("Add overlay", "オーバーレイを追加"));
	add->setProperty("primary", true);
	connect(add, &QPushButton::clicked, this, &ObsStandaloneDock::addOverlayToScene);
	overlayColumn->addWidget(add);
	layout->addWidget(overlayPanel);

	auto *storeButton = new QPushButton(
#ifdef __APPLE__
		textFor("More settings in the Mac app", "Mac アプリで詳細設定")
#elif defined(_WIN32)
		textFor("More settings in the Windows app", "Windows アプリで詳細設定")
#else
		textFor("More settings in Draw Stats", "Draw Stats で詳細設定")
#endif
	);
	connect(storeButton, &QPushButton::clicked, this, [this] {
		if (!client_->openPlatformStore() && statusLabel_)
			statusLabel_->setText(textFor("The Store could not be opened.", "ストアを開けませんでした。"));
	});
	layout->addWidget(storeButton);
	layout->addStretch(1);

	statusLabel_ = new QLabel;
	statusLabel_->setObjectName(QStringLiteral("muted"));
	layout->addWidget(statusLabel_);
	return page;
}

void ObsStandaloneDock::refresh()
{
	if (!pages_)
		return;
	if (!client_->isSignedIn()) {
		pages_->setCurrentIndex(0);
	} else if (client_->onboardingComplete() && pages_->currentIndex() != 6) {
		pages_->setCurrentIndex(5);
	} else if (pages_->currentIndex() == 0 || pages_->currentIndex() == 5) {
		pages_->setCurrentIndex(1);
	}
	if (statusLabel_)
		statusLabel_->setText(client_->statusText());
	if (authStatusLabel_)
		authStatusLabel_->setText(client_->statusText());
	if (scanLabel_)
		scanLabel_->setText(
			client_->isScanning()
				? textFor("Scanning installed and running apps…",
					  "インストール済み・実行中のアプリを検出しています…")
				: textFor(QStringLiteral("%1 apps checked").arg(client_->runningApps().size()),
					  QStringLiteral("%1件のアプリを確認しました")
						  .arg(client_->runningApps().size())));
	refreshSuggestions();
	const bool inputReady = client_->inputCaptureReady();
	const QString permissionState = client_->helperPermission();
	const QString permissionCopy =
		inputReady ? textFor("Ready. Input categories can be counted.", "準備完了。操作の種類を集計できます。")
		: permissionState == QStringLiteral("unknown")
			? textFor("Checking input access…", "入力権限を確認しています…")
			: textFor("Input Monitoring is off. Operations cannot be counted yet.",
				  "入力監視がオフです。まだ操作を集計できません。");
	if (permissionStatusLabel_)
		permissionStatusLabel_->setText(permissionCopy);
	if (permissionNextButton_)
		permissionNextButton_->setEnabled(inputReady);
	if (dashboardPermissionPanel_)
		dashboardPermissionPanel_->setVisible(!inputReady);
	if (dashboardPermissionLabel_)
		dashboardPermissionLabel_->setText(textFor(
			"Input Monitoring is required. Recording will stay stopped until operations can be counted.",
			"入力監視の許可が必要です。操作を集計できるまで記録は開始しません。"));
	if (recordingButton_) {
		recordingButton_->setText(client_->isRecording() ? textFor("Stop", "停止") : textFor("Record", "記録"));
		recordingButton_->setEnabled(client_->isRecording() ||
					     (inputReady && !client_->selectedTargetName().isEmpty()));
		recordingButton_->setProperty("recording", client_->isRecording());
		recordingButton_->style()->unpolish(recordingButton_);
		recordingButton_->style()->polish(recordingButton_);
	}
	const QJsonObject state = client_->dashboardState();
	if (recordingLabel_)
		recordingLabel_->setText(formatDuration(state.value("elapsedSeconds").toInteger()));
	if (focusLabel_)
		focusLabel_->setText(formatDuration(state.value("focusSeconds").toInteger()));
	if (metricsLabel_) {
		const QJsonObject metrics = state.value("metrics").toObject();
		metricsLabel_->setText(textFor(
			QStringLiteral("Keyboard  %1     Text  %2     Click  %3     Drag  %4     Wheel  %5")
				.arg(metrics.value("keyboard").toInt())
				.arg(metrics.value("text").toInt())
				.arg(metrics.value("click").toInt())
				.arg(metrics.value("drag").toInt())
				.arg(metrics.value("wheel").toInt()),
			QStringLiteral("キー  %1     文字  %2     クリック  %3     ドラッグ  %4     ホイール  %5")
				.arg(metrics.value("keyboard").toInt())
				.arg(metrics.value("text").toInt())
				.arg(metrics.value("click").toInt())
				.arg(metrics.value("drag").toInt())
				.arg(metrics.value("wheel").toInt())));
	}
	const bool hasTarget = !client_->selectedTargetName().isEmpty();
	if (targetNotice_)
		targetNotice_->setText(
			!hasTarget ? textFor("Choose a recording target.", "記録対象を選択してください。")
			: client_->targetAvailable()
				? textFor(QStringLiteral("Capturing %1").arg(client_->selectedTargetName()),
					  QStringLiteral("%1 を記録中").arg(client_->selectedTargetName()))
				: textFor(QStringLiteral("%1 is not open yet.").arg(client_->selectedTargetName()),
					  QStringLiteral("%1 はまだ開かれていません。")
						  .arg(client_->selectedTargetName())));
	if (openTargetButton_)
		openTargetButton_->setVisible(hasTarget && !client_->targetAvailable());
	if (timelineWidget_)
		timelineWidget_->update();
	refreshRecent();
}

void ObsStandaloneDock::refreshSuggestions()
{
	if (!suggestionsLayout_)
		return;
	while (QLayoutItem *item = suggestionsLayout_->takeAt(0)) {
		if (item->widget())
			item->widget()->deleteLater();
		delete item;
	}
	const QList<DrawingApp> suggestions = client_->suggestions();
	if (client_->isScanning()) {
		auto *progress = new QProgressBar;
		progress->setRange(0, 0);
		suggestionsLayout_->addWidget(progress);
	} else if (suggestions.isEmpty()) {
		const bool hasSavedTarget = !client_->selectedTargetName().isEmpty();
		auto *empty = new QLabel(
			hasSavedTarget && client_->targetAvailable()
				? textFor(QStringLiteral("%1 is selected and ready. Use Browse apps to change it.")
						  .arg(client_->selectedTargetName()),
					  QStringLiteral(
						  "%1 を選択済みです。記録を開始できます。「アプリを参照」から変更できます。")
						  .arg(client_->selectedTargetName()))
			: hasSavedTarget
				? textFor(QStringLiteral(
						  "%1 is selected, but is not currently detected. Use Browse apps to change it.")
						  .arg(client_->selectedTargetName()),
					  QStringLiteral(
						  "%1 を選択済みですが、現在は検出されていません。「アプリを参照」から変更できます。")
						  .arg(client_->selectedTargetName()))
				: textFor("No drawing app was detected. Choose one from Browse apps.",
					  "描画アプリを検出できませんでした。「アプリを参照」から選択してください。"));
		empty->setObjectName(QStringLiteral("muted"));
		empty->setWordWrap(true);
		suggestionsLayout_->addWidget(empty);
	} else {
		for (const DrawingApp &app : suggestions) {
			auto *button = new QPushButton(
				QStringLiteral("%1   ·   %2").arg(app.name, textFor("Detected", "検出済み")));
			button->setCheckable(true);
			button->setChecked(client_->selectedTargetName() == app.name);
			connect(button, &QPushButton::clicked, this, [this, app] { client_->selectCatalogApp(app); });
			suggestionsLayout_->addWidget(button);
		}
	}
	if (targetLabel_)
		targetLabel_->setText(
			client_->selectedTargetName().isEmpty()
				? textFor("Nothing selected", "未選択")
				: textFor(QStringLiteral("Selected: %1").arg(client_->selectedTargetName()),
					  QStringLiteral("選択中: %1").arg(client_->selectedTargetName())));
	if (appsNextButton_)
		appsNextButton_->setEnabled(!client_->selectedTargetName().isEmpty());
}

void ObsStandaloneDock::refreshRecent()
{
	if (!recentRowsLayout_)
		return;
	while (QLayoutItem *item = recentRowsLayout_->takeAt(0)) {
		if (item->widget())
			item->widget()->deleteLater();
		delete item;
	}
	const QJsonArray recent = client_->dashboardState().value("recent").toArray();
	const QHash<QString, QString> labels{{"keyboard", textFor("Keyboard", "キー操作")},
					     {"text", textFor("Text input", "文字入力")},
					     {"click", textFor("Click", "クリック")},
					     {"drag", textFor("Drag", "ドラッグ")},
					     {"wheel", textFor("Wheel", "ホイール")}};
	int shown = 0;
	for (const QJsonValue value : recent) {
		if (shown == 4)
			break;
		const QJsonObject event = value.toObject();
		const QString kind = event.value("kind").toString();
		const qint64 age = qMax<qint64>(
			0, (QDateTime::currentMSecsSinceEpoch() - static_cast<qint64>(event.value("at").toDouble())) /
				   1000);
		auto *row = new QWidget;
		auto *rowLayout = new QHBoxLayout(row);
		rowLayout->setContentsMargins(0, 0, 0, 0);
		rowLayout->setSpacing(7);
		auto *icon = new QLabel;
		icon->setPixmap(activityIcon(kind).pixmap(18, 18));
		icon->setFixedSize(20, 20);
		auto *label = new QLabel(labels.value(kind, kind));
		auto *time = new QLabel(QStringLiteral("%1s").arg(age));
		time->setObjectName(QStringLiteral("muted"));
		rowLayout->addWidget(icon);
		rowLayout->addWidget(label);
		rowLayout->addStretch();
		rowLayout->addWidget(time);
		recentRowsLayout_->addWidget(row);
		++shown;
	}
	if (recent.isEmpty()) {
		auto *empty = new QLabel(
			textFor("Activity will appear while you draw.", "描き始めると操作がここに表示されます。"));
		empty->setObjectName(QStringLiteral("muted"));
		recentRowsLayout_->addWidget(empty);
	}
	recentRowsLayout_->addStretch();
}

void ObsStandaloneDock::showBrowseDialog()
{
	const int currentPage = pages_->currentIndex();
	if (currentPage == 1 || currentPage == 5)
		browseReturnPage_ = currentPage;
	const auto clearOptions = [](QButtonGroup *group, QVBoxLayout *layout) {
		for (QAbstractButton *button : group->buttons())
			group->removeButton(button);
		while (QLayoutItem *item = layout->takeAt(0)) {
			if (item->widget())
				item->widget()->deleteLater();
			delete item;
		}
	};
	clearOptions(browseAppsGroup_, browseAppsLayout_);
	const QList<DrawingApp> catalog = client_->catalog();
	for (int index = 0; index < catalog.size(); ++index) {
		auto *button = new QPushButton(catalog.at(index).name);
		button->setCheckable(true);
		button->setProperty("browseOption", true);
		button->setChecked(client_->selectedTargetMode() == QStringLiteral("app") &&
				   client_->selectedTargetName() == catalog.at(index).name);
		browseAppsGroup_->addButton(button, index);
		browseAppsLayout_->addWidget(button);
	}
	browseAppsLayout_->addStretch();
	clearOptions(browseProcessesGroup_, browseProcessesLayout_);
	const QList<RunningApp> running = client_->runningApps();
	int processButtonId = 0;
	for (int index = 0; index < running.size(); ++index) {
		if (!running.at(index).running)
			continue;
		auto *button = new QPushButton(
			QStringLiteral("%1  ·  %2").arg(running.at(index).name, running.at(index).process));
		button->setCheckable(true);
		button->setProperty("browseOption", true);
		// Keep the exact scan-time identity. The background scan can refresh before Choose.
		button->setProperty("drawStatsRunningName", running.at(index).name);
		button->setProperty("drawStatsRunningProcess", running.at(index).process);
		button->setProperty("drawStatsRunningBundle", running.at(index).bundle);
		button->setProperty("drawStatsRunningPath", running.at(index).path);
		button->setProperty("drawStatsRunning", running.at(index).running);
		button->setChecked(client_->selectedTargetMode() == QStringLiteral("process") &&
				   (client_->selectedTargetName() == running.at(index).name ||
				    client_->selectedTargetName() == running.at(index).process));
		browseProcessesGroup_->addButton(button, processButtonId++);
		browseProcessesLayout_->addWidget(button);
	}
	browseProcessesLayout_->addStretch();
	refreshBrowseChooseState();
	pages_->setCurrentIndex(6);
}

void ObsStandaloneDock::refreshBrowseChooseState()
{
	if (!browseChooseButton_ || !browseTabs_)
		return;
	const bool hasSelection = browseTabs_->currentIndex() == 0 ? browseAppsGroup_->checkedId() >= 0
								   : browseProcessesGroup_->checkedId() >= 0;
	browseChooseButton_->setEnabled(hasSelection);
}

QWidget *ObsStandaloneDock::buildBrowsePage()
{
	auto *page = new QWidget;
	auto *layout = new QVBoxLayout(page);
	layout->setContentsMargins(4, 8, 4, 4);
	layout->setSpacing(9);
	auto *eyebrow = new QLabel(textFor("CAPTURE TARGET", "記録対象"));
	eyebrow->setObjectName(QStringLiteral("eyebrow"));
	auto *title = new QLabel(textFor("Browse apps", "アプリを参照"));
	title->setObjectName(QStringLiteral("pageTitle"));
	auto *copy = new QLabel(textFor("Choose a known drawing app, or match an exact process that is running now.",
					"既知の描画アプリ、または現在実行中のプロセスを選択します。"));
	copy->setObjectName(QStringLiteral("muted"));
	copy->setWordWrap(true);
	auto *search = new QLineEdit;
	search->setPlaceholderText(textFor("Search apps or processes", "アプリ・プロセスを検索"));
	browseTabs_ = new QTabWidget;
	browseAppsGroup_ = new QButtonGroup(this);
	browseAppsGroup_->setExclusive(true);
	browseProcessesGroup_ = new QButtonGroup(this);
	browseProcessesGroup_->setExclusive(true);
	auto *appsContent = new QWidget;
	browseAppsLayout_ = new QVBoxLayout(appsContent);
	browseAppsLayout_->setContentsMargins(5, 5, 5, 5);
	browseAppsLayout_->setSpacing(5);
	auto *appsScroll = new QScrollArea;
	appsScroll->setWidgetResizable(true);
	appsScroll->setFrameShape(QFrame::NoFrame);
	appsScroll->setWidget(appsContent);
	auto *processContent = new QWidget;
	browseProcessesLayout_ = new QVBoxLayout(processContent);
	browseProcessesLayout_->setContentsMargins(5, 5, 5, 5);
	browseProcessesLayout_->setSpacing(5);
	auto *processScroll = new QScrollArea;
	processScroll->setWidgetResizable(true);
	processScroll->setFrameShape(QFrame::NoFrame);
	processScroll->setWidget(processContent);
	browseTabs_->addTab(appsScroll, textFor("App name", "アプリ名"));
	browseTabs_->addTab(processScroll, textFor("Process name", "プロセス名"));
	connect(browseTabs_, &QTabWidget::currentChanged, this, [this] { refreshBrowseChooseState(); });
	connect(browseAppsGroup_, &QButtonGroup::idClicked, this, [this] { refreshBrowseChooseState(); });
	connect(browseProcessesGroup_, &QButtonGroup::idClicked, this, [this] { refreshBrowseChooseState(); });
	connect(search, &QLineEdit::textChanged, this, [this](const QString &query) {
		for (QButtonGroup *group : {browseAppsGroup_, browseProcessesGroup_})
			for (QAbstractButton *button : group->buttons())
				button->setVisible(button->text().contains(query, Qt::CaseInsensitive));
	});
	auto chooseCurrent = [this] {
		bool selected = false;
		if (browseTabs_->currentIndex() == 0 && browseAppsGroup_->checkedId() >= 0) {
			const int index = browseAppsGroup_->checkedId();
			const QList<DrawingApp> catalog = client_->catalog();
			if (index >= 0 && index < catalog.size()) {
				client_->selectCatalogApp(catalog.at(index));
				selected = true;
			}
		} else if (browseTabs_->currentIndex() == 1 && browseProcessesGroup_->checkedButton()) {
			QAbstractButton *button = browseProcessesGroup_->checkedButton();
			const RunningApp app{button->property("drawStatsRunningName").toString(),
					     button->property("drawStatsRunningProcess").toString(),
					     button->property("drawStatsRunningBundle").toString(),
					     button->property("drawStatsRunningPath").toString(),
					     button->property("drawStatsRunning").toBool()};
			if (!app.name.isEmpty() || !app.process.isEmpty()) {
				client_->selectRunningProcess(app);
				selected = true;
			}
		}
		if (selected)
			pages_->setCurrentIndex(browseReturnPage_);
	};
	auto *actions = new QHBoxLayout;
	auto *back = new QPushButton(textFor("Back", "戻る"));
	browseChooseButton_ = new QPushButton(textFor("Choose", "選択"));
	browseChooseButton_->setProperty("primary", true);
	browseChooseButton_->setEnabled(false);
	connect(back, &QPushButton::clicked, this, [this] { pages_->setCurrentIndex(browseReturnPage_); });
	connect(browseChooseButton_, &QPushButton::clicked, this, chooseCurrent);
	actions->addWidget(back);
	actions->addWidget(browseChooseButton_, 1);
	layout->addWidget(eyebrow);
	layout->addWidget(title);
	layout->addWidget(copy);
	layout->addWidget(search);
	layout->addWidget(browseTabs_, 1);
	layout->addLayout(actions);
	return page;
}

void ObsStandaloneDock::beginStartingTransition()
{
	if (!client_->inputCaptureReady()) {
		pages_->setCurrentIndex(2);
		return;
	}
	pages_->setCurrentIndex(4);
	QTimer::singleShot(1500, this, [this] {
		if (client_->startRecording()) {
			client_->setOnboardingComplete(true);
			pages_->setCurrentIndex(5);
		} else {
			pages_->setCurrentIndex(client_->inputCaptureReady() ? 3 : 2);
		}
	});
}

void ObsStandaloneDock::addOverlayToScene()
{
	const QByteArray sourceName("Draw Stats Overlay");
	obs_source_t *source = obs_get_source_by_name(sourceName.constData());
	if (!source) {
		obs_data_t *settings = obs_data_create();
		obs_data_set_string(settings, "url", client_->overlayUrl().toUtf8().constData());
		obs_data_set_int(settings, "width", 1280);
		obs_data_set_int(settings, "height", 720);
		obs_data_set_bool(settings, "shutdown", false);
		obs_data_set_bool(settings, "restart_when_active", false);
		source = obs_source_create("browser_source", sourceName.constData(), settings, nullptr);
		obs_data_release(settings);
	} else {
		obs_data_t *settings = obs_source_get_settings(source);
		obs_data_set_string(settings, "url", client_->overlayUrl().toUtf8().constData());
		obs_data_set_int(settings, "width", 1280);
		obs_data_set_int(settings, "height", 720);
		obs_source_update(source, settings);
		obs_data_release(settings);
	}
	if (!source) {
		statusLabel_->setText(
			textFor("OBS Browser Source is unavailable.", "OBSのブラウザソースを利用できません。"));
		return;
	}
	obs_source_t *sceneSource = obs_frontend_get_current_scene();
	obs_scene_t *scene = sceneSource ? obs_scene_from_source(sceneSource) : nullptr;
	obs_sceneitem_t *item = scene ? obs_scene_find_source(scene, sourceName.constData()) : nullptr;
	if (scene && !item)
		item = obs_scene_add(scene, source);
	if (item) {
		obs_video_info videoInfo{};
		const bool hasVideoInfo = obs_get_video_info(&videoInfo);
		const float canvasWidth = hasVideoInfo ? static_cast<float>(videoInfo.base_width) : 1280.0f;
		const float canvasHeight = hasVideoInfo ? static_cast<float>(videoInfo.base_height) : 720.0f;
		const float scale = qMin(canvasWidth / 1280.0f, canvasHeight / 720.0f);
		obs_transform_info transform{};
		transform.pos.x = (canvasWidth - 1280.0f * scale) / 2.0f;
		transform.pos.y = (canvasHeight - 720.0f * scale) / 2.0f;
		transform.scale.x = scale;
		transform.scale.y = scale;
		transform.alignment = OBS_ALIGN_LEFT | OBS_ALIGN_TOP;
		transform.bounds_type = OBS_BOUNDS_NONE;
		transform.bounds_alignment = OBS_ALIGN_CENTER;
		obs_sceneitem_set_info2(item, &transform);
		const obs_sceneitem_crop crop{};
		obs_sceneitem_set_crop(item, &crop);
		obs_sceneitem_set_visible(item, true);
	}
	if (sceneSource)
		obs_source_release(sceneSource);
	obs_source_release(source);
	statusLabel_->setText(
		textFor("Overlay added to the current scene.", "現在のシーンにオーバーレイを追加しました。"));
}

} // namespace draw_stats
