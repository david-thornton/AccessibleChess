/*
    This file is part of Cute Chess.
    Copyright (C) 2008-2018 Cute Chess authors

    Cute Chess is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Cute Chess is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Cute Chess.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "cutechessapp.h"

#include <QCoreApplication>
#include <QDir>
#include <QTime>
#include <QFileInfo>
#include <QSettings>

#include <mersenne.h>
#include <enginemanager.h>
#include <gamemanager.h>
#include <board/boardfactory.h>
#include <chessgame.h>
#include <timecontrol.h>
#include <humanbuilder.h>
#include <enginebuilder.h>

#include "mainwindow.h"
#include "settingsdlg.h"
#include "tournamentresultsdlg.h"
#include "gamedatabasedlg.h"
#include "gamedatabasemanager.h"
#include "importprogressdlg.h"
#include "pgnimporter.h"
#include "gamewall.h"
#include "python/voiceassistant.h"
#ifndef Q_OS_WIN32
#	include <sys/types.h>
#	include <pwd.h>
#endif


CuteChessApplication::CuteChessApplication(int& argc, char* argv[])
	: QApplication(argc, argv),
	  m_settingsDialog(nullptr),
	  m_tournamentResultsDialog(nullptr),
	  m_engineManager(nullptr),
	  m_gameManager(nullptr),
	  m_gameDatabaseManager(nullptr),
	  m_gameDatabaseDialog(nullptr),
	  m_gameWall(nullptr),
	  m_initialWindowCreated(false),
	  m_voiceAssistant()
{
	Mersenne::initialize(QTime(0,0,0).msecsTo(QTime::currentTime()));

	// Set the application icon
	QIcon icon;
	icon.addFile(":/icons/cutechess_512x512.png");
	icon.addFile(":/icons/cutechess_256x256.png");
	icon.addFile(":/icons/cutechess_128x128.png");
	icon.addFile(":/icons/cutechess_64x64.png");
	icon.addFile(":/icons/cutechess_32x32.png");
	icon.addFile(":/icons/cutechess_24x24.png");
	icon.addFile(":/icons/cutechess_16x16.png");
	setWindowIcon(icon);

	setQuitOnLastWindowClosed(false);

	QCoreApplication::setOrganizationName("cutechess");
	QCoreApplication::setOrganizationDomain("cutechess.com");
	QCoreApplication::setApplicationName("cutechess");
	QCoreApplication::setApplicationVersion(CUTECHESS_VERSION);

	// Use Ini format on all platforms
	QSettings::setDefaultFormat(QSettings::IniFormat);

	// Load the engines
	engineManager()->loadEngines(configPath() + QLatin1String("/engines.json"));
	m_engineManager->addEngine(EngineConfiguration(
		"stockfish",
		STOCKFISH_EXECUTABLE_PATH_STRING,
		"uci"
	));

	// Read the game database state
	gameDatabaseManager()->readState(configPath() + QLatin1String("/gamedb.bin"));

	connect(this, SIGNAL(lastWindowClosed()), this, SLOT(onLastWindowClosed()));
	connect(this, SIGNAL(aboutToQuit()), this, SLOT(onAboutToQuit()));
}

CuteChessApplication::~CuteChessApplication()
{
	delete m_gameDatabaseDialog;
	delete m_settingsDialog;
	delete m_tournamentResultsDialog;
	delete m_gameWall;
}

CuteChessApplication* CuteChessApplication::instance()
{
	return static_cast<CuteChessApplication*>(QApplication::instance());
}

QString CuteChessApplication::userName()
{
	#ifdef Q_OS_WIN32
	return qgetenv("USERNAME");
	#else
	if (QSettings().value("ui/use_full_user_name", true).toBool())
	{
		auto pwd = getpwnam(qgetenv("USER"));
		if (pwd != nullptr)
			return QString(pwd->pw_gecos).split(',')[0];
	}
	return qgetenv("USER");
	#endif
}

QString CuteChessApplication::configPath()
{
	// We want to have the exact same config path in "gui" and
	// "cli" applications so that they can share resources
	QSettings settings;
	QFileInfo fi(settings.fileName());
	QDir dir(fi.absolutePath());

	if (!dir.exists())
		dir.mkpath(fi.absolutePath());

	return fi.absolutePath();
}

EngineManager* CuteChessApplication::engineManager()
{
	if (m_engineManager == nullptr)
		m_engineManager = new EngineManager(this);

	return m_engineManager;
}

GameManager* CuteChessApplication::gameManager()
{
	if (m_gameManager == nullptr)
	{
		m_gameManager = new GameManager(this);
		// int concurrency = QSettings()
		// 	.value("tournament/concurrency", 1).toInt();
		// Only allow one game at a time.
		m_gameManager->setConcurrency(1);
	}

	return m_gameManager;
}

QList<MainWindow*> CuteChessApplication::gameWindows()
{
	m_gameWindows.removeAll(nullptr);

	QList<MainWindow*> gameWindowList;
	const auto gameWindows = m_gameWindows;
	for (const auto& window : gameWindows)
		gameWindowList << window.data();

	return gameWindowList;
}

MainWindow* CuteChessApplication::newGameWindow(ChessGame* game)
{
	MainWindow* mainWindow = new MainWindow(game);
	m_gameWindows.prepend(mainWindow);
	mainWindow->show();
	m_initialWindowCreated = true;
	emit game->gameWindowCreated(mainWindow);

	return mainWindow;
}

void CuteChessApplication::newDefaultGame()
{
	
	// default game is a human versus human game using standard variant and
	// infinite time control
	std::unique_ptr<PgnGame> pgn_game(new PgnGame());
	std::unique_ptr<ChessGame> game(new ChessGame(Chess::BoardFactory::create("standard"), pgn_game.get()));
	pgn_game.release();

	game->setTimeControl(TimeControl("inf"));
	game->pause();

	connect(game.get(), SIGNAL(started(ChessGame*)),
		this, SLOT(newGameWindow(ChessGame*)));
	connect(game.get(), &ChessGame::started, &m_voiceAssistant, &VoiceAssistant::onGameStarted);

	std::unique_ptr<HumanBuilder> human_builder1(new HumanBuilder(userName()));
	std::unique_ptr<HumanBuilder> human_builder2(new HumanBuilder(userName()));
	// std::unique_ptr<EngineBuilder> engine_builder(new EngineBuilder(
	// 	m_engineManager->engineAt(m_engineManager->engineCount() - 1)
	// ));
	gameManager()->newGame(game.release(), human_builder1.release(), human_builder2.release());
}

void CuteChessApplication::showGameWindow(int index)
{
	auto gameWindow = m_gameWindows.at(index);
	gameWindow->activateWindow();
	gameWindow->raise();
}

GameDatabaseManager* CuteChessApplication::gameDatabaseManager()
{
	if (m_gameDatabaseManager == nullptr)
		m_gameDatabaseManager = new GameDatabaseManager(this);

	return m_gameDatabaseManager;
}

void CuteChessApplication::showSettingsDialog()
{
	if (m_settingsDialog == nullptr)
		m_settingsDialog = new SettingsDialog();

	showDialog(m_settingsDialog);
}

void CuteChessApplication::showTournamentResultsDialog()
{
	showDialog(tournamentResultsDialog());
}

TournamentResultsDialog*CuteChessApplication::tournamentResultsDialog()
{
	if (m_tournamentResultsDialog == nullptr)
		m_tournamentResultsDialog = new TournamentResultsDialog();

	return m_tournamentResultsDialog;
}

VoiceAssistant* CuteChessApplication::voiceAssistant() {
	return &m_voiceAssistant;
}

void CuteChessApplication::showGameDatabaseDialog()
{
	if (m_gameDatabaseDialog == nullptr)
		m_gameDatabaseDialog = new GameDatabaseDialog(gameDatabaseManager());

	showDialog(m_gameDatabaseDialog);
}

void CuteChessApplication::showGameWall()
{
	if (m_gameWall == nullptr)
	{
		m_gameWall = new GameWall(gameManager());
		auto flags = m_gameWall->windowFlags();
		m_gameWall->setWindowFlags(flags | Qt::Window);
		m_gameWall->setAttribute(Qt::WA_DeleteOnClose, true);
		m_gameWall->setWindowTitle(tr("Active Games"));
	}

	showDialog(m_gameWall);
}

void CuteChessApplication::onQuitAction()
{
	closeDialogs();
	closeAllWindows();
}

void CuteChessApplication::onLastWindowClosed()
{
	if (!m_initialWindowCreated)
		return;

	if (m_gameManager != nullptr)
	{
		connect(m_gameManager, SIGNAL(finished()), this, SLOT(quit()));
		m_gameManager->finish();
	}
	else
		quit();
}

void CuteChessApplication::onAboutToQuit()
{
	if (gameDatabaseManager()->isModified())
		gameDatabaseManager()->writeState(configPath() + QLatin1String("/gamedb.bin"));
}

void CuteChessApplication::showDialog(QWidget* dlg)
{
	Q_ASSERT(dlg != nullptr);

	if (dlg->isMinimized())
		dlg->showNormal();
	else
		dlg->show();

	dlg->raise();
	dlg->activateWindow();
}

void CuteChessApplication::closeDialogs()
{
	if (m_tournamentResultsDialog)
		m_tournamentResultsDialog->close();
	if (m_gameDatabaseDialog)
		m_gameDatabaseDialog->close();
	if (m_settingsDialog)
		m_settingsDialog->close();
	if (m_gameWall)
		m_gameWall->close();
}
