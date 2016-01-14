// albert - a simple application launcher for linux
// Copyright (C) 2014-2015 Manuel Schneider
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

#include <QCursor>
#include <QDebug>
#include <QDesktopWidget>
#include <QDir>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>
#include <QQmlContext>
#include <QQuickItem>
#include "mainwindow.h"
#include "albertapp.h"

const QString MainWindow::CFG_CENTERED        = "showCentered";
const bool    MainWindow::DEF_CENTERED        = true;
const QString MainWindow::CFG_HIDEONFOCUSLOSS = "hideOnFocusLoss";
const bool    MainWindow::DEF_HIDEONFOCUSLOSS = true;
const QString MainWindow::CFG_ALWAYS_ON_TOP   = "alwaysOnTop";
const bool    MainWindow::DEF_ALWAYS_ON_TOP   = true;
const QString MainWindow::CFG_IS_TOOL         = "isToolWindow";
const bool    MainWindow::DEF_IS_TOOL         = false;
const QString MainWindow::CFG_STYLEPATH       = "stylePath";
const QUrl    MainWindow::DEF_STYLEPATH       = QUrl("qrc:/resources/ui/MainComponent.qml");
const QString MainWindow::CFG_MAX_PROPOSALS   = "itemCount";
const int     MainWindow::DEF_MAX_PROPOSALS   = 5;
const QString MainWindow::CFG_WND_POS         = "windowPosition";

/** ***************************************************************************/
MainWindow::MainWindow(QWindow *parent)
    : QQuickView(parent),
      history_(QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath("history.dat")){
    setColor(Qt::transparent);
    setFlags(Qt::Dialog
             | Qt::WindowStaysOnTopHint
             | Qt::FramelessWindowHint
             | Qt::WindowCloseButtonHint // No close event w/o this
             );

    // Set qml environment
    rootContext()->setContextProperty("history", &history_);
    rootContext()->setContextProperty("resultsModel", &model_);

    // Load settings
    QSettings s;
    setPosition(s.value(CFG_WND_POS).toPoint());
    setShowCentered(s.value(CFG_CENTERED, DEF_CENTERED).toBool());
    setHideOnFocusLoss(s.value(CFG_HIDEONFOCUSLOSS, DEF_HIDEONFOCUSLOSS).toBool());
    setAlwaysOnTop(s.value(CFG_ALWAYS_ON_TOP, DEF_ALWAYS_ON_TOP).toBool());
    setIsTool(s.value(CFG_IS_TOOL, DEF_IS_TOOL).toBool());
    setSource(s.value(CFG_STYLEPATH, DEF_STYLEPATH).toUrl());
    setMaxProposals(s.value(CFG_MAX_PROPOSALS, DEF_MAX_PROPOSALS).toUInt()); // Dont set before source
    hide();
}



/** ***************************************************************************/
MainWindow::~MainWindow() {
    // Save settings
    QSettings s;
    s.setValue(CFG_CENTERED, showCentered_);
    s.setValue(CFG_HIDEONFOCUSLOSS, hideOnFocusLoss_);
    s.setValue(CFG_ALWAYS_ON_TOP, alwaysOnTop());
    s.setValue(CFG_IS_TOOL, isTool());
    s.setValue(CFG_STYLEPATH, source());
    s.setValue(CFG_MAX_PROPOSALS, maxProposals());
    s.setValue(CFG_WND_POS, position());
}



/** ***************************************************************************/
void MainWindow::show() {
    if (showCentered_){
        QDesktopWidget *dw = QApplication::desktop();
        setPosition(dw->availableGeometry(dw->screenNumber(QCursor::pos()))
                    .center()-QPoint(width()/2,192));
    }
    QQuickView::show();
    raise();
    requestActivate();
}



/** ***************************************************************************/
void MainWindow::hide() {
    QQuickView::hide();
}



/** ***************************************************************************/
void MainWindow::toggleVisibility() {
    isVisible() ? hide() : show();
}



/** ***************************************************************************/
void MainWindow::setSource(const QUrl &url) {
    QQuickView::setSource(url);
    QObject *object = rootObject();
    connect(object, SIGNAL(queryChanged(QString)), this, SIGNAL(queryChanged(const QString&)));
    connect(object, SIGNAL(indexActivated(int)), this, SIGNAL(indexActivated(int)));
    connect(object, SIGNAL(settingsWindowRequested()), this, SIGNAL(settingsWindowRequested()));
    connect(this, SIGNAL(visibleChanged(bool)), object, SLOT(onMainWindowVisibleChanged()));
}



/** ***************************************************************************/
void MainWindow::setModel(QAbstractItemModel *model) {
    model_.setSourceModel(model);
}



/** ***************************************************************************/
bool MainWindow::event(QEvent *event) {
    switch (event->type())
    {
    // Quit on Alt+F4
    case QEvent::Close:
            qApp->quit();
        break;

    // Hide window on escape key
    case QEvent::KeyPress:
        if ( static_cast<QKeyEvent*>(event)->modifiers() == Qt::NoModifier
             && static_cast<QKeyEvent*>(event)->key() == Qt::Key_Escape )
            hide();
        else if ( static_cast<QKeyEvent*>(event)->modifiers() == Qt::AltModifier
                  && static_cast<QKeyEvent*>(event)->key() == Qt::Key_F4 )
            qApp->quit();
        break;

    case QEvent::FocusOut:
        /* This is a horribly hackish but working solution.

         A triggered key grab on X11 steals the focus of the window for short
         period of time. This may result in the following annoying behaviour:
         When the hotkey is pressed and X11 steals the focus there arises a
         race condition between the hotkey event and the focus out event.
         When the app is visible and the focus out event is delivered the app
         gets hidden. Finally when the hotkey is received the app gets shown
         again although the user intended to hide the app with the hotkey.

         Solutions:
         Although X11 differs between the two focus out events, qt does not.
         One might install a native event filter and use the XCB structs to
         decide which type of event is delivered, but this approach is not
         platform independent (unless designed so explicitely, but its a
         hassle). The behaviour was expected when the app hides on:

         (mode==XCB_NOTIFY_MODE_GRAB && detail==XCB_NOTIFY_DETAIL_NONLINEAR)||
          (mode==XCB_NOTIFY_MODE_NORMAL && detail==XCB_NOTIFY_DETAIL_NONLINEAR)
         (Check Xlib Programming Manual)

         The current, much simpler but less elegant solution is to delay the
         hiding a few milliseconds, so that the hotkey event will always be
         handled first. */
        if (static_cast<QFocusEvent*>(event)->reason() == Qt::ActiveWindowFocusReason
                && hideOnFocusLoss_ && !isActive()){
            QTimer::singleShot(50, this, &MainWindow::hide);
        }
        break;
    default:break;
    }
    return QQuickView::event(event);
}



/** ***************************************************************************/
uint MainWindow::maxProposals() const {
    return rootObject()->property("maxProposals").toUInt();
}



/** ***************************************************************************/
void MainWindow::setMaxProposals(uint maxProposals) {
    rootObject()->setProperty("maxProposals", maxProposals);
}



/** ***************************************************************************/
bool MainWindow::alwaysOnTop() const {
    return flags() & Qt::WindowStaysOnTopHint;
}



/** ***************************************************************************/
void MainWindow::setAlwaysOnTop(bool alwaysOnTop) {
    alwaysOnTop
            ? setFlags(flags() | Qt::WindowStaysOnTopHint)
            : setFlags(flags() & ~Qt::WindowStaysOnTopHint);
    // Flags changed. Update
    QQuickView::hide();
}



/** ***************************************************************************/
bool MainWindow::isTool() const {
    return flags() & Qt::Tool;
}



/** ***************************************************************************/
void MainWindow::setIsTool(bool isTool) {
    // Tool = Dialog|Popup
    isTool
            ? setFlags(flags() | Qt::Popup)
            : setFlags((flags() & ~Qt::Popup));
    // Flags changed. Update
    QQuickView::hide();
}



/** ***************************************************************************/
bool MainWindow::hideOnFocusLoss() const {
    return hideOnFocusLoss_;
}



/** ***************************************************************************/
void MainWindow::setHideOnFocusLoss(bool hideOnFocusLoss) {
    hideOnFocusLoss_ = hideOnFocusLoss;
}



/** ***************************************************************************/
bool MainWindow::showCentered() const {
    return showCentered_;
}



/** ***************************************************************************/
void MainWindow::setShowCentered(bool showCentered) {
    showCentered_ = showCentered;
}
