#include "commandwindow.h"
#include "ui_commandwindow.h"
#include <parser/outputparser.h>
#include <parser/statusparser.h>
#include <QLineEdit>
#include <QDateTime>
#include <utility> // std::move
#include <QDesktopServices>
#include <QFile>
#include <QDir>
#include <QCompleter>
#include <QMessageBox>

QString CommandWindow::logFileNameFormat = "log_%1_%2.log";
QString CommandWindow::autoCompletionFileName = "commands.txt";
QString CommandWindow::preferencesFileName = "preferences.ini";

CommandWindow::CommandWindow(Server server, QMainWindow* mainWindow, QWidget* parent) :
    QMainWindow(parent),
    ui(new Ui::CommandWindow),
    mainWindow(mainWindow),
    preferences(preferencesFileName, QSettings::IniFormat),
    disconnect(false)
{
    ui->setupUi(this);
    lastCommand = QDateTime::currentDateTime().addDays(-1);
    logFileName = QString(logFileNameFormat).arg(server.getIp()).arg(server.getPort());
    status = new Query(server.getIp(), server.getPort());
    rcon = new Rcon(std::move(server));
    playerModel = new PlayerTableModel(this);
    ui->playerTableView->setModel(playerModel);
    connect(status, SIGNAL(receive(QByteArray)), this, SLOT(onReceiveStatus(QByteArray)));
    connect(rcon, SIGNAL(receive(QByteArray)), this, SLOT(onReceiveRcon(QByteArray)));
    connect(ui->commandBox->lineEdit(), SIGNAL(returnPressed()), this, SLOT(on_sendButton_clicked()));
    connect(&statusTimer, SIGNAL(timeout()), this, SLOT(requestServerStatus()));
    baseWindowTitle = windowTitle();
    statusTimer.setInterval(preferences.value("getstatus_interval", 2000).toInt());
    statusTimer.start();
    loadAutoCompletionCommands();
    QTimer::singleShot(250, this, SLOT(on_actionStatus_triggered()));
}

CommandWindow::~CommandWindow()
{
    delete ui;
    delete playerModel;
    delete status;
    delete rcon;
}

void CommandWindow::closeEvent(QCloseEvent* event)
{
    if (mainWindow != nullptr) {
        if (disconnect)
            mainWindow->show();
        else
            mainWindow->close();
    }

    QMainWindow::closeEvent(event);
}

void CommandWindow::resizeEvent(QResizeEvent *)
{
    ui->playerTableView->setFixedWidth((int)(this->size().width() * 0.25));
}

void CommandWindow::on_sendButton_clicked()
{
    sendCommand(ui->commandBox->currentText().toUtf8());
}

void CommandWindow::onReceiveStatus(QByteArray output)
{
    Status status = StatusParser::parse(output);
    QMap<QString, QString> v = std::move(status.variables);
    QList<Player> p = std::move(status.players);
    int ping = this->status->getPing();
    QString serverStatus = (ping > 0) ? QString(" ~ %1 ms").arg(ping) : "";
    setWindowTitle(QString("%1 - %2 %3 [%4/%5]%6").arg(baseWindowTitle)
                                                  .arg(v.value("gamename"))
                                                  .arg(v.value("shortversion"))
                                                  .arg(p.size())
                                                  .arg(v.value("sv_maxclients"))
                                                  .arg(serverStatus));

    QString hostname = OutputParser::removeColors(v.value("sv_hostname"));
    ui->statusbar->showMessage(QString("%1 (%2) - %3").arg(v.value("mapname"))
                                                      .arg(v.value("g_gametype"))
                                                      .arg(hostname));
    playerModel->setPlayers(p);
}

void CommandWindow::onReceiveRcon(QByteArray output)
{
    QList<Output> parsedOutput = OutputParser::parse(output);
    QListIterator<Output> i(parsedOutput);
    QTextCursor prevCursor = ui->commandOutput->textCursor();
    ui->commandOutput->moveCursor(QTextCursor::End);
    while (i.hasNext()) {
        Output line = i.next();
        writeToLog(line.getText());
        ui->commandOutput->insertHtml(line.toHtml());
    }

    ui->commandOutput->setTextCursor(prevCursor);
}

void CommandWindow::requestServerStatus()
{
    status->send("getstatus");
}

void CommandWindow::on_actionStatus_triggered()
{
    sendCommand("status");
}

void CommandWindow::on_actionDisconnect_triggered()
{
    disconnect = true;
    close();
}

void CommandWindow::on_actionServer_info_triggered()
{
    sendCommand("serverinfo");
}

void CommandWindow::on_actionExit_triggered()
{
    close();
}

void CommandWindow::on_actionPreferences_triggered()
{
    openFileAsDefault(preferencesFileName);
}

void CommandWindow::on_actionView_log_triggered()
{
    openFileAsDefault(logFileName);
}

void CommandWindow::on_actionAuto_completion_commands_triggered()
{
    openFileAsDefault(autoCompletionFileName);
}

void CommandWindow::on_actionPlayer_list_triggered()
{
    ui->playerTableView->setVisible(!ui->playerTableView->isVisible());
}

void CommandWindow::sendCommand(QByteArray command)
{
    QDateTime now = QDateTime::currentDateTime();
    if (lastCommand.msecsTo(now) < 1000) {
        return; // Prevent from spamming the server
    }

    lastCommand = now;
    ui->commandOutput->moveCursor(QTextCursor::End);
    QString output = QDateTime::currentDateTime().toString()
                                                 .append(" > ")
                                                 .append(command);
    writeToLog(output + "\n\n");
    ui->commandOutput->insertHtml(output.append("<br /><br />"));
    rcon->send(command);
}

void CommandWindow::writeToLog(QString line)
{
    if (preferences.value("logging_enabled", 1) == "0") {
        return;
    }

    QFile log(logFileName);
    if (log.open(QFile::WriteOnly | QFile::Append)) {
        QTextStream out(&log);
        out << line;
    }
}

void CommandWindow::loadAutoCompletionCommands()
{
    QFile file(autoCompletionFileName);
    file.open(QFile::ReadOnly);
    QTextStream in(&file);
    QList<QString> autoCompletionCommands;
    while (!in.atEnd()) {
        QString command = in.readLine();
        if (!command.isEmpty() && !autoCompletionCommands.contains(command)) {
            autoCompletionCommands.append(command);
        }
    }

    QCompleter* completer = new QCompleter(autoCompletionCommands, this);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    ui->commandBox->setCompleter(completer);
}

void CommandWindow::openFileAsDefault(QString fileName)
{
    if (QApplication::keyboardModifiers().testFlag(Qt::ControlModifier)) {
        QDesktopServices::openUrl(QUrl(QDir(fileName).currentPath()));
    } else {
        if (QFile(fileName).exists()) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(fileName));
        } else {
            QMessageBox::critical(this,
                                  QApplication::applicationName(),
                                  QString("'%1' does not exist.").arg(fileName));
        }
    }
}
