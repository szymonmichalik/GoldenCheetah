/*
 * Copyright (c) 2012 Damien Grauser (Damien.Grauser@pev-geneve.ch)  *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "BlankState.h"
#include <QtGui>
#include "MainWindow.h"

#ifdef Q_OS_MAC
#include "QtMacButton.h" // mac
#endif

//
// Replace home window when no ride
//
BlankStatePage::BlankStatePage(MainWindow *main) : main(main)
{
    QHBoxLayout *homeLayout = new QHBoxLayout(this);
    homeLayout->setAlignment(Qt::AlignCenter);
    homeLayout->addSpacing(20); // left margin

    // left part
    QWidget *left = new QWidget(this);
    leftLayout = new QVBoxLayout(left);
    leftLayout->setAlignment(Qt::AlignLeft | Qt::AlignBottom);
    left->setLayout(leftLayout);

    welcomeTitle = new QLabel(left);
    welcomeTitle->setFont(QFont("Helvetica", 30, QFont::Bold, false));
    leftLayout->addWidget(welcomeTitle);

    welcomeText = new QLabel(left);
    welcomeText->setFont(QFont("Helvetica", 16, QFont::Light, false));
    leftLayout->addWidget(welcomeText);

    leftLayout->addSpacing(10);

    homeLayout->addWidget(left);
    homeLayout->addSpacing(50);

    QWidget *right = new QWidget(this);
    QVBoxLayout *rightLayout = new QVBoxLayout(right);
    rightLayout->setAlignment(Qt::AlignRight | Qt::AlignBottom);
    right->setLayout(rightLayout);

    img = new QToolButton(this);
    img->setToolButtonStyle(Qt::ToolButtonIconOnly);
    img->setStyleSheet("QToolButton {text-align: left;color : blue;background: transparent}");
    rightLayout->addWidget(img);

    homeLayout->addWidget(right);
    // right margin
    homeLayout->addSpacing(20);

    setLayout(homeLayout);
}

QPushButton*
BlankStatePage::addToShortCuts(ShortCut shortCut)
{
    //
    // Separator
    //
    if (shortCuts.count()>0) {
        leftLayout->addSpacing(20);
        QFrame* line = new QFrame();
        line->setFrameShape(QFrame::HLine);
        line->setFrameShadow(QFrame::Sunken);
        leftLayout->addWidget(line);
    }

    // append to the list of shortcuts
    shortCuts.append(shortCut);

    //
    // Create text and button
    //
    QLabel *shortCutLabel = new QLabel(this);
    shortCutLabel->setText(shortCut.label);
    shortCutLabel->setFont(QFont("Helvetica", 16, QFont::Light, false));
    leftLayout->addWidget(shortCutLabel);

    QPushButton *shortCutButton = new QPushButton(this);
    shortCutButton->setText(shortCut.buttonLabel);
    shortCutButton->setIcon(QPixmap(shortCut.buttonIconPath));
    shortCutButton->setIconSize(QSize(60,60));
    //importButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    //importButton->setStyleSheet("QToolButton {text-align: left;color : blue;background: transparent}");
    shortCutButton->setStyleSheet("QPushButton {border-radius: 10px;border-style: outset; background-color: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1, stop: 0 #DDDDDD, stop: 1 #BBBBBB); border-width: 1px; border-color: #555555;} QPushButton:pressed {background-color: qlineargradient(x1: 0, y1: 0, x2: 0, y2: 1, stop: 0 #BBBBBB, stop: 1 #999999);}");
    shortCutButton->setFixedWidth(200);
    leftLayout->addWidget(shortCutButton);

    return shortCutButton;
}

//
// Replace analysis window when no ride
//
BlankStateAnalysisPage::BlankStateAnalysisPage(MainWindow *main) : BlankStatePage(main)
{  
    welcomeTitle->setText("Analysis");
    welcomeText->setText("No ride ?\nLet's start with some data.");

    img->setIcon(QPixmap(":images/analysis.png"));
    img->setIconSize(QSize(800,330));

    ShortCut scImport;
    scImport.label = tr("Import files from your disc or usb device");
    scImport.buttonLabel = tr("Import data");
    scImport.buttonIconPath = ":images/mac/download.png";
    QPushButton *importButton = addToShortCuts(scImport);
    connect(importButton, SIGNAL(clicked()), main, SLOT(importFile()));

    ShortCut scDownload;
    scDownload.label = tr("Download from serial device.");
    scDownload.buttonLabel = tr("Download from device");
    scDownload.buttonIconPath = ":images/mac/download.png";
    QPushButton *downloadButton = addToShortCuts(scDownload);
    connect(downloadButton, SIGNAL(clicked()), main, SLOT(downloadRide()));

}

//
// Replace home window when no ride
//
BlankStateHomePage::BlankStateHomePage(MainWindow *main) : BlankStatePage(main)
{
    welcomeTitle->setText("Home");
    welcomeText->setText("No ride ?\nLet's start with some data.");

    img->setIcon(QPixmap(":images/home.png"));
    img->setIconSize(QSize(800,330));

    /*ShortCut scImport;
    scImport.label = tr("Import files from your disc or usb device");
    scImport.buttonLabel = tr("Import data");
    scImport.buttonIconPath = ":images/mac/download.png";
    addToShortCuts(scImport);

    ShortCut scDownload;
    scDownload.label = tr("Download from serial device.");
    scDownload.buttonLabel = tr("Download from device");
    scDownload.buttonIconPath = ":images/mac/download.png";
    addToShortCuts(scDownload);*/
}

//
// Replace diary window when no ride
//
BlankStateDiaryPage::BlankStateDiaryPage(MainWindow *main) : BlankStatePage(main)
{
    welcomeTitle->setText("Diary");
    welcomeText->setText("No ride ?\nLet's start with some data.");

    img->setIcon(QPixmap(":images/diary.png"));
    img->setIconSize(QSize(800,330));

    /*ShortCut scImport;
    scImport.label = tr("Import files from your disc or usb device");
    scImport.buttonLabel = tr("Import data");
    scImport.buttonIconPath = ":images/mac/download.png";
    addToShortCuts(scImport);

    ShortCut scDownload;
    scDownload.label = tr("Download from serial device.");
    scDownload.buttonLabel = tr("Download from device");
    scDownload.buttonIconPath = ":images/mac/download.png";
    addToShortCuts(scDownload);*/
}

//
// Replace train window when no ride
//
BlankStateTrainPage::BlankStateTrainPage(MainWindow *main) : BlankStatePage(main)
{
    welcomeTitle->setText("Train");
    welcomeText->setText("No ride ?\nLet's start with some data.");

    img->setIcon(QPixmap(":images/train.png"));
    img->setIconSize(QSize(800,330));

    ShortCut scAddDevice;
    // - add a realtime device
    // - find video and workouts
    scAddDevice.label = tr("Find and add a training devices.");
    scAddDevice.buttonLabel = tr("Add device");
    scAddDevice.buttonIconPath = ":images/mac/download.png";
    QPushButton *addDeviceButton = addToShortCuts(scAddDevice);
    connect(addDeviceButton, SIGNAL(clicked()), main, SLOT(addDevice()));


    ShortCut scImportWorkout;
    scImportWorkout.label = tr("Find and Import your videos and workouts.");
    scImportWorkout.buttonLabel = tr("Scan hard drives");
    scImportWorkout.buttonIconPath = ":images/toolbar/Disk.png";
    QPushButton *importWorkoutButton = addToShortCuts(scImportWorkout);
    connect(importWorkoutButton, SIGNAL(clicked()), main, SLOT(manageLibrary()));
}
