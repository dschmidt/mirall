/*
 * Copyright (C) by Duncan Mac-Vicar P. <duncan@kde.org>
 * Copyright (C) by Klaas Freitag <freitag@owncloud.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License
 * for more details.
 */

#include <QDebug>
#include <QDir>
#include <QUrl>
#include <QMutexLocker>
#include <QThread>
#include <QStringList>
#include <QTextStream>
#include <QTimer>

#include "csync.h"

#include "mirall/owncloudfolder.h"
#include "mirall/mirallconfigfile.h"

namespace Mirall {

#define POLL_TIMER_EXCEED 10

ownCloudFolder::ownCloudFolder(const QString &alias,
                               const QString &path,
                               const QString &secondPath,
                               QObject *parent)
    : Folder(alias, path, secondPath, parent)
    , _secondPath(secondPath)
    , _localCheckOnly( false )
    , _localFileChanges( false )
    , _csync(0)
    , _pollTimerCnt(0)
    , _csyncError(false)
    , _lastSeenFiles(0)
{
#ifdef USE_INOTIFY
    qDebug() << "****** ownCloud folder using watcher *******";
    // The folder interval is set in the folder parent class.
#else
    /* If local polling is used, the polltimer of class Folder has to fire more
     * often
     * Set a local poll time of 2000 milliseconds, which results in a 30 seconds
     * remote poll interval, defined in slotPollTimerRemoteCheck
     */

    _pollTimer->stop();
    connect( _pollTimer, SIGNAL(timeout()), this, SLOT(slotPollTimerRemoteCheck()));
    setPollInterval( 2000 );
    _pollTimer->start();
    qDebug() << "****** ownCloud folder using local poll *******";
#endif
}

ownCloudFolder::~ownCloudFolder()
{
}

#ifndef USE_INOTIFY
void ownCloudFolder::slotPollTimerRemoteCheck()
{
    _pollTimerCnt++;
    qDebug() << "**** Poll Timer Cnt increase: " << _pollTimerCnt;
}
#endif

bool ownCloudFolder::isBusy() const
{
    return false;
}

QString ownCloudFolder::secondPath() const
{
    QString re(_secondPath);
    MirallConfigFile cfg;
    const QString ocUrl = cfg.ownCloudUrl(QString(), true);
    qDebug() << "**** " << ocUrl << " <-> " << re;
    if( re.startsWith( ocUrl ) ) {
        re.remove( ocUrl );
    }

    return re;
}

void ownCloudFolder::startSync()
{
    startSync( QStringList() );
}

void ownCloudFolder::startSync(const QStringList &pathList)
{
    Folder::startSync( pathList );

    if (_csync && _csync->isRunning()) {
        qCritical() << "* ERROR csync is still running and new sync requested.";
        return;
    }
    delete _csync;
    _errors.clear();
    _csyncError = false;

    MirallConfigFile cfgFile;

    QUrl url( _secondPath );
    if( url.scheme() == QString::fromLocal8Bit("http") ) {
        url.setScheme( "owncloud" );
    } else {
        // connect SSL!
        url.setScheme( "ownclouds" );
    }

#ifdef USE_INOTIFY
    // if there is a watcher and no polling, ever sync is remote.
    _localCheckOnly = false;
#else
    _localCheckOnly = true;
    if( _pollTimerCnt == POLL_TIMER_EXCEED || _localFileChanges ) {
        _localCheckOnly = false;
        _pollTimerCnt = 0;
        _localFileChanges = false;
    }
#endif
    qDebug() << "*** Start syncing to ownCloud, onlyLocal: " << _localCheckOnly;

    _csync = new CSyncThread( path(), url.toEncoded(), _localCheckOnly );
    _csync->setUserPwd( cfgFile.ownCloudUser(), cfgFile.ownCloudPasswd() );
    QObject::connect(_csync, SIGNAL(started()),  SLOT(slotCSyncStarted()));
    QObject::connect(_csync, SIGNAL(finished()), SLOT(slotCSyncFinished()));
    QObject::connect(_csync, SIGNAL(terminated()), SLOT(slotCSyncTerminated()));
    connect(_csync, SIGNAL(csyncError(const QString)), SLOT(slotCSyncError(const QString)));

    connect( _csync, SIGNAL(treeWalkResult(WalkStats*)),
             this, SLOT(slotThreadTreeWalkResult(WalkStats*)));
    _csync->start();
}

void ownCloudFolder::slotCSyncStarted()
{
    qDebug() << "    * csync thread started";
    emit syncStarted();
}

void ownCloudFolder::slotThreadTreeWalkResult( WalkStats *wStats )
{
    qDebug() << "Seen files: " << wStats->seenFiles;

    /* check if there are happend changes in the file system */
    qDebug() << "New     files: " << wStats->newFiles;
    qDebug() << "Updated files: " << wStats->eval;
    qDebug() << "Walked  files: " << wStats->seenFiles;
    qDebug() << "Eval files: "    << wStats->eval;
    qDebug() << "Removed files: " << wStats->removed;
    qDebug() << "Renamed files: " << wStats->renamed;

    if( ! _localCheckOnly ) _lastSeenFiles = 0;
    _localFileChanges = false;

#ifndef USE_INOTIFY
    if( _lastSeenFiles > 0 && _lastSeenFiles != wStats->seenFiles ) {
        qDebug() << "*** last seen files different from currently seen number " << _lastSeenFiles << "<>" << wStats->seenFiles << " => full Sync needed";
        _localFileChanges = true;
    }
    if( (wStats->newFiles + wStats->eval + wStats->removed + wStats->renamed) > 0 ) {
         qDebug() << "*** Local changes, lets do a full sync!" ;
         _localFileChanges = true;
    }
    if( _pollTimerCnt < POLL_TIMER_EXCEED ) {
        qDebug() << "     *** No local changes, finalize, pollTimerCounter is "<< _pollTimerCnt ;
    }
#endif
    _lastSeenFiles = wStats->seenFiles;

    /*
     * Attention: This is deleted here, outside of the thread, because the thread can
     * faster die than this routine has read out the memory.
     */
    delete wStats->sourcePath;
    delete wStats;
}

void ownCloudFolder::slotCSyncError(const QString& err)
{
    _errors.append( err );
    _csyncError = true;
}

void ownCloudFolder::slotCSyncTerminated()
{
    // do not ask csync here for reasons.
    SyncResult res( SyncResult::Error );
    _errors.append( tr("The CSync thread terminated unexpectedly.") );
    res.setErrorStrings(_errors);

    emit syncFinished( res );
}

void ownCloudFolder::slotCSyncFinished()
{
    SyncResult res( SyncResult::Success );

    if (_csyncError) {
        res.setStatus(SyncResult::Error);

        qDebug() << "  ** error Strings: " << _errors;
        res.setErrorStrings( _errors );
        qDebug() << "    * owncloud csync thread finished with error";
    } else {
        qDebug() << "    * owncloud csync thread finished successfully " << _localCheckOnly;
    }

    if( ! _localCheckOnly ) _lastSeenFiles = 0;

    emit syncFinished( res );
}

} // ns

