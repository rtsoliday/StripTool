#include "ui/help.h"
#include "core/application.h"
#include "services/runtime_capabilities.h"
#include "ui/about_banner.h"

#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QApplication>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QScreen>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QSplitter>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextDocument>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtMath>

namespace striptool {
namespace {
const QUrl manualUrl(QStringLiteral("https://ops.aps.anl.gov/manuals/QtStripTool/"));

struct Topic {
  QString anchor, title, markdown;
};

QVector<Topic> helpTopics() {
  QVector<Topic> topics;
  QFile guide(QStringLiteral(":/help/user-guide.md"));
  if (guide.open(QIODevice::ReadOnly)) {
    const QString text = QString::fromUtf8(guide.readAll());
    const QRegularExpression heading(QStringLiteral("^## (.+)$"),
                                      QRegularExpression::MultilineOption);
    auto headings = heading.globalMatch(text);
    while (headings.hasNext()) {
      const auto match = headings.next();
      const auto next = heading.match(text, match.capturedEnd());
      const QString title = match.captured(1).trimmed();
      QString anchor = title.toLower();
      anchor.remove(QRegularExpression(QStringLiteral("[^\\w\\s-]")));
      anchor.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral("-"));
      topics.append({anchor, title, text.mid(match.capturedEnd(),
          (next.hasMatch() ? next.capturedStart() : text.size())
              - match.capturedEnd()).trimmed()});
    }
  }
  topics.append({QStringLiteral("keyboard-shortcuts"), QObject::tr("Keyboard shortcuts"),
      QObject::tr(
          "Use Command instead of Ctrl on macOS.\n\n"
          "| Where | Keys | Action |\n| --- | --- | --- |\n"
          "| Graph or Controls | F1 | Open Help Topics |\n"
          "| Help | Ctrl+F | Search the guide |\n"
          "| Help | Enter or F3 | Next match |\n"
          "| Help | Shift+Enter or Shift+F3 | Previous match |\n"
          "| Help | Esc | Close Help |\n"
          "| Controls | Return in the PV field | Connect the PV |\n"
          "| Controls | Return in a curve's Name, Minimum, or Maximum | Apply Modify |\n"
          "| Graph | Delete or Backspace | Remove the selected annotation |\n\n"
          "Help search ignores case, highlights every match, and wraps around.\n\n"
          "### Graph mouse controls\n\n"
          "- **Pan:** drag empty plot space with the left button.\n"
          "- **Zoom time:** turn the mouse wheel; the time under the pointer stays put.\n"
          "- **Smaller pan or zoom step:** right click a toolbar Pan or Zoom button.\n"
          "- **Commands:** right click the graph.\n"
          "- **Annotations:** double click empty plot space to add one, or an "
          "existing one to edit it. Left click to select; hold the middle button "
          "on it to move it.\n"
          "- **Read a curve:** click its legend entry to read its value at the pointer.")});
  return topics;
}

QString topicHtml(const Topic& topic) {
  QTextDocument document;
  document.setMarkdown(topic.markdown);
  // Qt's Markdown import gives list items after a heading the heading's bold
  // block format, which toHtml() then applies to the whole item.
  for (auto block = document.begin(); block.isValid(); block = block.next())
    if (block.textList()) QTextCursor(block).setBlockCharFormat(QTextCharFormat());
  QString html = document.toHtml();
  const auto body = QRegularExpression(QStringLiteral("<body[^>]*>")).match(html);
  html = html.mid(body.capturedEnd());
  html.truncate(html.lastIndexOf(QStringLiteral("</body>")));
  return QStringLiteral("<h2><a name=\"%1\">%2</a></h2>%3")
      .arg(topic.anchor.toHtmlEscaped(), topic.title.toHtmlEscaped(), html);
}

class HelpWindow final : public QDialog {
public:
  explicit HelpWindow(QWidget* parent) : QDialog(parent), topics_(helpTopics()) {
    setObjectName(QStringLiteral("helpWindow"));
    setWindowTitle(tr("Qt StripTool Help"));
    setModal(false);
    setWindowFlags((windowFlags() | Qt::WindowMinMaxButtonsHint)
                   & ~Qt::WindowContextHelpButtonHint);
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 14, 14, 12);
    layout->setSpacing(10);
    auto* findRow = new QHBoxLayout;
    find_ = new QLineEdit(this);
    find_->setObjectName(QStringLiteral("helpFind"));
    find_->setPlaceholderText(tr("Find in help"));
    find_->setAccessibleName(tr("Find in help"));
    find_->setClearButtonEnabled(true);
    previous_ = new QToolButton(this);
    next_ = new QToolButton(this);
    previous_->setObjectName(QStringLiteral("helpPrevious"));
    next_->setObjectName(QStringLiteral("helpNext"));
    previous_->setArrowType(Qt::UpArrow);
    next_->setArrowType(Qt::DownArrow);
    previous_->setToolTip(tr("Previous match (Shift+Enter / Shift+F3)"));
    next_->setToolTip(tr("Next match (Enter / F3)"));
    previous_->setAccessibleName(tr("Previous match"));
    next_->setAccessibleName(tr("Next match"));
    previous_->setAutoRaise(true);
    next_->setAutoRaise(true);
    matchesLabel_ = new QLabel(this);
    matchesLabel_->setObjectName(QStringLiteral("helpMatches"));
    matchesLabel_->setMinimumWidth(matchesLabel_->fontMetrics().horizontalAdvance(tr("No matches")));
    findRow->addWidget(find_, 1);
    findRow->addWidget(previous_);
    findRow->addWidget(next_);
    findRow->addWidget(matchesLabel_);
    layout->addLayout(findRow);

    list_ = new QListWidget(this);
    list_->setObjectName(QStringLiteral("helpTopics"));
    list_->setAccessibleName(tr("Help topics"));
    list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    list_->setWordWrap(true);
    browser_ = new QTextBrowser(this);
    browser_->setObjectName(QStringLiteral("helpBrowser"));
    browser_->setAccessibleName(tr("Help text"));
    browser_->setOpenLinks(false);
    browser_->document()->setDocumentMargin(14);
    browser_->document()->setDefaultStyleSheet(QStringLiteral(
        "h2 { color: %1; margin-top: 22px; margin-bottom: 6px; } "
        "h3 { margin-top: 14px; margin-bottom: 4px; } "
        "a { color: %1; } th { background-color: %2; text-align: left; } "
        "td, th { padding: 5px; }")
        .arg(palette().color(QPalette::Link).name(),
             palette().color(QPalette::AlternateBase).name()));
    auto* splitter = new QSplitter(this);
    splitter->setObjectName(QStringLiteral("helpSplitter"));
    splitter->addWidget(list_);
    splitter->addWidget(browser_);
    splitter->setChildrenCollapsible(false);
    splitter->setStretchFactor(1, 1);
    layout->addWidget(splitter, 1);
    QString html;
    for (const auto& topic : topics_) {
      auto* item = new QListWidgetItem(topic.title, list_);
      item->setData(Qt::UserRole, topic.anchor);
      html += topicHtml(topic);
    }
    browser_->setHtml(html);
    // Explicit anchors keep topic selection, scrolling, and search in sync.
    for (const auto& topic : topics_) {
      int position = -1;
      for (auto block = browser_->document()->begin(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it)
          if (it.fragment().charFormat().anchorNames().contains(topic.anchor))
            position = block.position();
        if (position >= 0) break;
      }
      positions_.append(position);
    }
    splitter->setSizes({260, 680});
    setStyleSheet(QStringLiteral(
        "QListWidget#helpTopics { border: 1px solid %1; border-radius: 6px; padding: 4px; } "
        "QListWidget#helpTopics::item { padding: 7px 8px; border-radius: 4px; } "
        "QListWidget#helpTopics::item:selected { background: %2; color: %3; } "
        "QTextBrowser#helpBrowser { border: 1px solid %1; border-radius: 6px; }")
        .arg(palette().color(QPalette::Mid).name(),
             palette().color(QPalette::Highlight).name(),
             palette().color(QPalette::HighlightedText).name()));

    auto* buttons = new QHBoxLayout;
    auto* online = new QPushButton(tr("Online manual"), this);
    online->setObjectName(QStringLiteral("helpOnlineManual"));
    online->setAutoDefault(false);
    auto* close = new QPushButton(tr("Close"), this);
    close->setObjectName(QStringLiteral("helpClose"));
    close->setAutoDefault(false);
    buttons->addWidget(online);
    buttons->addStretch();
    buttons->addWidget(close);
    layout->addLayout(buttons);
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    connect(online, &QPushButton::clicked, this, [] { QDesktopServices::openUrl(helpManualUrl()); });
    connect(list_, &QListWidget::currentRowChanged, this, [this](int row) {
      if (row >= 0) showTopic(topics_[row].anchor);
    });
    connect(browser_->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { followScroll(); });
    connect(browser_, &QTextBrowser::anchorClicked, this, [this](const QUrl& url) {
      if (url.isRelative() && url.path().isEmpty()) showTopic(url.fragment());
      else QDesktopServices::openUrl(url.isRelative() ? manualUrl.resolved(url) : url);
    });
    connect(find_, &QLineEdit::textChanged, this, [this] { updateMatches(); });
    connect(find_, &QLineEdit::returnPressed, this, [this] {
      findNext(QApplication::keyboardModifiers() & Qt::ShiftModifier ? -1 : 1);
    });
    connect(previous_, &QToolButton::clicked, this, [this] { findNext(-1); });
    connect(next_, &QToolButton::clicked, this, [this] { findNext(1); });
    auto shortcut = [this](const QList<QKeySequence>& keys, auto callback) {
      auto* action = new QAction(this);
      action->setShortcuts(keys);
      addAction(action);
      connect(action, &QAction::triggered, this, callback);
    };
    shortcut(QKeySequence::keyBindings(QKeySequence::Find), [this] {
      find_->setFocus();
      find_->selectAll();
    });
    auto nextKeys = QKeySequence::keyBindings(QKeySequence::FindNext);
    if (!nextKeys.contains(QKeySequence(Qt::Key_F3))) nextKeys.append(QKeySequence(Qt::Key_F3));
    shortcut(nextKeys, [this] { findNext(1); });
    auto previousKeys = QKeySequence::keyBindings(QKeySequence::FindPrevious);
    const QKeySequence shiftF3(Qt::SHIFT | Qt::Key_F3);
    if (!previousKeys.contains(shiftF3)) previousKeys.append(shiftF3);
    shortcut(previousKeys, [this] { findNext(-1); });
    updateMatches();
    resize(980, 680);
    if (auto* screen = this->screen())
      resize(size().boundedTo(screen->availableGeometry().size() - QSize(40, 60)));
    showTopic(QStringLiteral("start-the-application"));
  }

  void showTopic(const QString& anchor) {
    for (int i = 0; i < topics_.size(); ++i) {
      if (topics_[i].anchor != anchor) continue;
      QScopedValueRollback<bool> guard(navigating_, true);
      selectTopic(i);
      browser_->scrollToAnchor(anchor);
      return;
    }
  }

private:
  QVector<Topic> topics_;
  QVector<int> positions_;
  QListWidget* list_;
  QTextBrowser* browser_;
  QLineEdit* find_;
  QToolButton *previous_, *next_;
  QLabel* matchesLabel_;
  QVector<QTextCursor> matches_;
  int currentMatch_ = -1;
  bool navigating_ = false;

  void selectTopic(int index) {
    QSignalBlocker blocker(list_);
    list_->setCurrentRow(index);
    list_->scrollToItem(list_->currentItem());
  }

  int topicAt(int position) const {
    int current = 0;
    for (int i = 0; i < positions_.size(); ++i)
      if (positions_[i] >= 0 && positions_[i] <= position) current = i;
    return current;
  }

  void followScroll() {
    if (navigating_) return;
    int current = 0;
    auto* document = browser_->document();
    const int top = browser_->verticalScrollBar()->value() + 24;
    for (int i = 0; i < positions_.size(); ++i)
      if (positions_[i] >= 0 && document->documentLayout()->blockBoundingRect(
          document->findBlock(positions_[i])).top() <= top) current = i;
    selectTopic(current);
  }

  void updateMatches() {
    const int start = currentMatch_ >= 0 ? matches_[currentMatch_].selectionStart()
        : browser_->cursorForPosition(QPoint(0, 0)).position();
    matches_.clear();
    currentMatch_ = -1;
    if (!find_->text().isEmpty()) {
      QTextCursor cursor(browser_->document());
      while (!(cursor = browser_->document()->find(find_->text(), cursor)).isNull()) {
        if (currentMatch_ < 0 && cursor.selectionStart() >= start) currentMatch_ = matches_.size();
        matches_.append(cursor);
      }
      if (currentMatch_ < 0 && !matches_.isEmpty()) currentMatch_ = 0;
    }
    showMatches();
  }

  void findNext(int direction) {
    if (matches_.isEmpty()) return;
    currentMatch_ = (currentMatch_ + direction + matches_.size()) % matches_.size();
    showMatches();
  }

  void showMatches() {
    QList<QTextEdit::ExtraSelection> selections;
    QColor highlight = browser_->palette().color(QPalette::Highlight);
    highlight.setAlpha(55);
    for (int i = 0; i < matches_.size(); ++i) {
      QTextEdit::ExtraSelection selection;
      selection.cursor = matches_[i];
      selection.format.setBackground(i == currentMatch_
          ? browser_->palette().color(QPalette::Highlight) : highlight);
      if (i == currentMatch_)
        selection.format.setForeground(browser_->palette().color(QPalette::HighlightedText));
      selections.append(selection);
    }
    browser_->setExtraSelections(selections);
    previous_->setEnabled(!matches_.isEmpty());
    next_->setEnabled(!matches_.isEmpty());
    matchesLabel_->setText(find_->text().isEmpty() ? QString()
        : matches_.isEmpty() ? tr("No matches")
        : tr("%1 of %2").arg(currentMatch_ + 1).arg(matches_.size()));
    if (currentMatch_ < 0) return;
    QScopedValueRollback<bool> guard(navigating_, true);
    // Extra selections own the highlights; keep the normal cursor collapsed
    // so clearing Find also removes the current match's visible selection.
    auto cursor = matches_[currentMatch_];
    cursor.setPosition(cursor.selectionStart());
    browser_->setTextCursor(cursor);
    browser_->ensureCursorVisible();
    selectTopic(topicAt(matches_[currentMatch_].selectionStart()));
  }
};
}

QUrl helpManualUrl() {
  const QString configured = qEnvironmentVariable("STRIP_HELP_PATH");
  if (configured.isEmpty()) return manualUrl;
  const QUrl supplied(configured);
  return supplied.isValid() && supplied.scheme().size() > 1
      ? supplied : QUrl::fromLocalFile(QFileInfo(configured).absoluteFilePath());
}

QDialog* showHelp(QWidget* parent, const QString& topic) {
  auto* help = parent ? static_cast<HelpWindow*>(parent->findChild<QDialog*>(
      QStringLiteral("helpWindow"), Qt::FindDirectChildrenOnly)) : nullptr;
  if (!help) help = new HelpWindow(parent);
  help->showNormal();
  if (!topic.isEmpty()) help->showTopic(topic);
  help->raise();
  help->activateWindow();
  return help;
}

void showAbout(QWidget* parent) {
  QDialog dialog(parent);
  dialog.setObjectName(QStringLiteral("aboutDialog"));
  dialog.setWindowTitle(QObject::tr("About Qt StripTool"));
  dialog.setWindowFlag(Qt::WindowContextHelpButtonHint, false);
  auto* layout = new QVBoxLayout(&dialog);
  layout->setContentsMargins(18, 18, 18, 14);
  layout->setSpacing(6);
  layout->addWidget(new AboutBanner(&dialog));
  auto* title = new QLabel(applicationName(), &dialog);
  title->setObjectName(QStringLiteral("aboutTitle"));
  QFont font = title->font();
  font.setPointSizeF(font.pointSizeF() > 0 ? font.pointSizeF() * 1.7 : 18);
  font.setBold(true);
  title->setFont(font);
  layout->addWidget(title);
  auto* subtitle = new QLabel(QObject::tr("Version %1 · EPICS Strip Chart Tool")
      .arg(applicationVersion()), &dialog);
  subtitle->setObjectName(QStringLiteral("aboutSubtitle"));
  subtitle->setWordWrap(true);
  layout->addWidget(subtitle);
  auto* credits = new QTextBrowser(&dialog);
  credits->setObjectName(QStringLiteral("aboutCredits"));
  credits->setAccessibleName(QObject::tr("Author credits"));
  credits->setFrameShape(QFrame::NoFrame);
  auto creditsPalette = credits->palette();
  creditsPalette.setColor(QPalette::Base, dialog.palette().color(QPalette::Window));
  credits->setPalette(creditsPalette);
  credits->document()->setDocumentMargin(0);
  credits->setHtml(aboutText());
  credits->setOpenExternalLinks(true);
  layout->addWidget(credits, 1);
  auto* footer = new QLabel(QObject::tr("Qt %1 · EPICS %2").arg(qtVersion(), epicsVersion()), &dialog);
  footer->setObjectName(QStringLiteral("aboutFooter"));
  footer->setForegroundRole(QPalette::PlaceholderText);
  footer->setWordWrap(true);
  footer->setTextInteractionFlags(Qt::TextSelectableByMouse);
  layout->addWidget(footer);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok, &dialog);
  auto* help = buttons->addButton(QObject::tr("Open help"), QDialogButtonBox::HelpRole);
  help->setObjectName(QStringLiteral("aboutOpenHelp"));
  bool openHelp = false;
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(help, &QPushButton::clicked, &dialog, [&] {
    openHelp = true;
    dialog.accept();
  });
  layout->addWidget(buttons);
  buttons->button(QDialogButtonBox::Ok)->setDefault(true);
  buttons->button(QDialogButtonBox::Ok)->setFocus();
  // Show all credits when space permits, and retain scrolling on small
  // screens or with large desktop fonts instead of clipping the last authors.
  // Measure a detached copy: a hidden QTextBrowser lays out its own document
  // for its unshown viewport, which greatly overstates the height.
  QScopedPointer<QTextDocument> measure(credits->document()->clone());
  measure->setTextWidth(464);
  const int chromeHeight = dialog.sizeHint().height() - credits->sizeHint().height();
  QSize desired(500, chromeHeight + qCeil(measure->size().height()) + 12);
  if (auto* screen = dialog.screen())
    desired = desired.boundedTo(screen->availableGeometry().size() - QSize(40, 60));
  dialog.resize(desired);
  dialog.exec();
  if (openHelp) showHelp(parent, QStringLiteral("start-the-application"));
}

void populateHelpMenu(QMenu* menu, QWidget* owner, const QString& prefix, const QString& topic) {
  auto name = [&prefix](QString suffix) {
    if (prefix.isEmpty()) return suffix;
    suffix[0] = suffix[0].toUpper();
    return prefix + suffix;
  };
  auto* help = menu->addAction(QObject::tr("Help &Topics"));
  help->setObjectName(name(QStringLiteral("helpAction")));
  auto keys = QKeySequence::keyBindings(QKeySequence::HelpContents);
  if (!keys.contains(QKeySequence(Qt::Key_F1))) keys.append(QKeySequence(Qt::Key_F1));
  help->setShortcuts(keys);
  QObject::connect(help, &QAction::triggered, owner, [owner, topic] { showHelp(owner, topic); });
  auto* shortcuts = menu->addAction(QObject::tr("&Keyboard Shortcuts"));
  shortcuts->setObjectName(name(QStringLiteral("keyboardShortcutsAction")));
  QObject::connect(shortcuts, &QAction::triggered, owner, [owner] {
    showHelp(owner, QStringLiteral("keyboard-shortcuts"));
  });
  auto* online = menu->addAction(QObject::tr("&Online Manual"));
  online->setObjectName(name(QStringLiteral("onlineManualAction")));
  QObject::connect(online, &QAction::triggered, owner, [] { QDesktopServices::openUrl(helpManualUrl()); });
  menu->addSeparator();
  auto* about = menu->addAction(QObject::tr("&About Qt StripTool"));
  about->setObjectName(name(QStringLiteral("aboutAction")));
  QObject::connect(about, &QAction::triggered, owner, [owner] { showAbout(owner); });
}
}
