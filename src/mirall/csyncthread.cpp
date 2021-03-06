/*
 * Copyright (C) by Duncan Mac-Vicar P. <duncan@kde.org>
 * Copyright (C) by Klaas Freitag <freitag@owncloud.com>
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
#include <QMutexLocker>
#include <QThread>
#include <QStringList>
#include <QTextStream>
#include <QTime>
#include <QDebug>

#include "mirall/csyncthread.h"
#include "mirall/mirallconfigfile.h"

namespace Mirall {

/* static variables to hold the credentials */
QString CSyncThread::_user;
QString CSyncThread::_passwd;
QMutex CSyncThread::_mutex;

 int CSyncThread::checkPermissions( TREE_WALK_FILE* file, void *data )
 {
     WalkStats *wStats = static_cast<WalkStats*>(data);

     if( !wStats ) {
         qDebug() << "WalkStats is zero - must not be!";
         return -1;
     }

     wStats->seenFiles++;

     switch(file->instruction) {
     case CSYNC_INSTRUCTION_NONE:

         break;
     case CSYNC_INSTRUCTION_EVAL:
         wStats->eval++;
         break;
     case CSYNC_INSTRUCTION_REMOVE:
         wStats->removed++;
         break;
     case CSYNC_INSTRUCTION_RENAME:
         wStats->renamed++;
         break;
     case CSYNC_INSTRUCTION_NEW:
         wStats->newFiles++;
         break;
     case CSYNC_INSTRUCTION_CONFLICT:
         wStats->conflicts++;
         break;
     case CSYNC_INSTRUCTION_IGNORE:
         wStats->ignores++;
         break;
     case CSYNC_INSTRUCTION_SYNC:
         wStats->sync++;
         break;
     case CSYNC_INSTRUCTION_STAT_ERROR:
     case CSYNC_INSTRUCTION_ERROR:
         /* instructions for the propagator */
     case CSYNC_INSTRUCTION_DELETED:
     case CSYNC_INSTRUCTION_UPDATED:
         wStats->error++;
         wStats->errorType = WALK_ERROR_INSTRUCTIONS;
         break;
     default:
         wStats->error++;
         wStats->errorType = WALK_ERROR_WALK;
         break;
     }

     if( file ) {
         QString source(wStats->sourcePath);
         source.append(file->path);
         QFileInfo fi(source);

         if( fi.isDir()) {  // File type directory.
            if( !(fi.isWritable() && fi.isExecutable()) ) {
                 wStats->errorType = WALK_ERROR_DIR_PERMS;
             }
         }
     }

     // qDebug() << wStats->seenFiles << ". Path: " << file->path << ": uid= " << file->uid << " - type: " << file->type;
     if( wStats->errorType != WALK_ERROR_NONE ) {
         return -1;
     }
     return 0;
 }

CSyncThread::CSyncThread(const QString &source, const QString &target, bool localCheckOnly)

    : _source(source)
    , _target(target)
    , _localCheckOnly( localCheckOnly )

{
    _mutex.lock();
    if( ! _source.endsWith('/')) _source.append('/');
    _mutex.unlock();
}

CSyncThread::~CSyncThread()
{

}

void CSyncThread::run()
{
    CSYNC *csync;

    WalkStats *wStats = new WalkStats;
    QTime walkTime;

    wStats->sourcePath = 0;
    wStats->errorType  = 0;
    wStats->eval       = 0;
    wStats->removed    = 0;
    wStats->renamed    = 0;
    wStats->newFiles   = 0;
    wStats->ignores    = 0;
    wStats->sync       = 0;
    wStats->seenFiles  = 0;
    wStats->conflicts  = 0;
    wStats->error      = 0;

    _mutex.lock();
    if( csync_create(&csync,
                          _source.toLocal8Bit().data(),
                          _target.toLocal8Bit().data()) < 0 ) {
        emit csyncError( tr("CSync create failed.") );
    }
    // FIXME: Check if we really need this stringcopy!
    wStats->sourcePath = qstrdup( _source.toLocal8Bit().constData() );
    _mutex.unlock();

    qDebug() << "## CSync Thread local only: " << _localCheckOnly;
    csync_set_auth_callback( csync, getauth );
    csync_enable_conflictcopys(csync);


    MirallConfigFile cfg;
    QString excludeList = cfg.excludeFile();

    if( !excludeList.isEmpty() ) {
        qDebug() << "==== added CSync exclude List: " << excludeList.toAscii();
        csync_add_exclude_list( csync, excludeList.toAscii() );
    }

    QTime t;
    t.start();

    _mutex.lock();
    if( _localCheckOnly ) {
        csync_set_local_only( csync, true );
    }
    _mutex.unlock();

    if( csync_init(csync) < 0 ) {
        CSYNC_ERROR_CODE err = csync_errno();
        QString errStr;

        switch( err ) {
        case CSYNC_ERR_LOCK:
            errStr = tr("CSync failed to create a lock file.");
            break;
        case CSYNC_ERR_STATEDB_LOAD:
            errStr = tr("CSync failed to load the state db.");
            break;
        case CSYNC_ERR_MODULE:
            errStr = tr("CSync failed to load the ownCloud module.");
            break;
        case CSYNC_ERR_TIMESKEW:
            errStr = tr("The system time between the local machine and the server differs "
                        "too much. Please use a time syncronization service (ntp) on both machines.");
            break;
        case CSYNC_ERR_FILESYSTEM:
            errStr = tr("CSync could not detect the filesystem type.");
            break;
        case CSYNC_ERR_TREE:
            errStr = tr("CSync got an error while processing internal trees.");
            break;
        default:
            errStr = tr("An internal error number %1 happend.").arg( (int) err );
        }
        qDebug() << " #### ERROR String emitted: " << errStr;
        emit csyncError(errStr);
        goto cleanup;
    }

    qDebug() << "############################################################### >>";
    if( csync_update(csync) < 0 ) {
        emit csyncError(tr("CSync Update failed."));
        goto cleanup;
    }
    qDebug() << "<<###############################################################";

    csync_set_userdata(csync, wStats);

    walkTime.start();
    if( csync_walk_local_tree(csync, &checkPermissions, 0) < 0 ) {
        qDebug() << "Error in treewalk.";
        if( wStats->errorType == WALK_ERROR_DIR_PERMS ) {
            emit csyncError(tr("The local filesystem has directories which are write protected.\n"
                               "That prevents ownCloud from successful syncing.\n"
                               "Please make sure that all directories are writeable."));
        } else if( wStats->errorType == WALK_ERROR_WALK ) {
            emit csyncError(tr("CSync encountered an error while examining the file system.\n"
                               "Syncing is not possible."));
        } else if( wStats->errorType == WALK_ERROR_INSTRUCTIONS ) {
            emit csyncError(tr("CSync update generated a strange instruction.\n"
                               "Please write a bug report."));
        }
        emit csyncError(tr("Local filesystem problems. Better disable Syncing and check."));
        goto cleanup;
    }
    qDebug() << " ..... Local walk finished: " << walkTime.elapsed();

    // emit the treewalk results. Do not touch the wStats after this.
    emit treeWalkResult(wStats);

    _mutex.lock();
    if( _localCheckOnly ) {
        _mutex.unlock();
        // we have to go out here as its local check only.
        goto cleanup;
    } else {
        _mutex.unlock();
        // check if we can write all over.

        if( csync_reconcile(csync) < 0 ) {
            emit csyncError(tr("CSync reconcile failed."));
            goto cleanup;
        }
        if( csync_propagate(csync) < 0 ) {
            emit csyncError(tr("CSync propagate failed."));
            goto cleanup;
        }
    }
cleanup:
    csync_destroy(csync);
    /*
     * Attention: do not delete the wStat memory here. it is deleted in the
     * slot catching the signel treeWalkResult because this thread can faster
     * die than the slot has read out the data.
     */
    qDebug() << "CSync run took " << t.elapsed() << " Milliseconds";
}


void CSyncThread::setUserPwd( const QString& user, const QString& passwd )
{
    _mutex.lock();
    _user = user;
    _passwd = passwd;
    _mutex.unlock();
}

int CSyncThread::getauth(const char *prompt,
                         char *buf,
                         size_t len,
                         int echo,
                         int verify,
                         void *userdata
                         )
{
    QString qPrompt = QString::fromLocal8Bit( prompt ).trimmed();
    _mutex.lock();

    if( qPrompt == QString::fromLocal8Bit("Enter your username:") ) {
        // qDebug() << "OOO Username requested!";
        strncpy( buf, _user.toLocal8Bit().constData(), len );
    } else if( qPrompt == QString::fromLocal8Bit("Enter your password:") ) {
        // qDebug() << "OOO Password requested!";
        strncpy( buf, _passwd.toLocal8Bit().constData(), len );
    } else {
        if( qPrompt.startsWith( QLatin1String("There are problems with the SSL certificate:"))) {
            // SSL is requested. If the program came here, the SSL check was done by mirall
            // the answer is simply yes here.
            strncpy( buf, "yes", 3 );
        } else {
            qDebug() << "Unknown prompt: <" << prompt << ">";
        }
    }
    _mutex.unlock();
}

}
