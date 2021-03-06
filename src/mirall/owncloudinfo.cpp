/*
 * Copyright (C) by Klaas Freitag <freitag@kde.org>
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


#include <QtCore>
#include <QtGui>
#include <QAuthenticator>


#include "mirall/owncloudinfo.h"
#include "mirall/mirallconfigfile.h"
#include "mirall/sslerrordialog.h"
#include "mirall/version.h"
#include "mirall/sslerrordialog.h"

namespace Mirall
{

QNetworkAccessManager* ownCloudInfo::_manager = 0;
SslErrorDialog *ownCloudInfo::_sslErrorDialog = 0;
bool            ownCloudInfo::_certsUntrusted = false;


ownCloudInfo::ownCloudInfo( const QString& connectionName, QObject *parent ) :
    QObject(parent)
{
    if( connectionName.isEmpty() )
        _connection = QString::fromLocal8Bit( "ownCloud");
    else
        _connection = connectionName;

    if( ! _manager ) {
        qDebug() << "Creating static NetworkAccessManager";
        _manager = new QNetworkAccessManager;
    }

    connect( _manager, SIGNAL( sslErrors(QNetworkReply*, QList<QSslError>)),
             this, SLOT(slotSSLFailed(QNetworkReply*, QList<QSslError>)) );

    connect( _manager, SIGNAL(authenticationRequired(QNetworkReply*, QAuthenticator*)),
             SLOT(slotAuthentication(QNetworkReply*,QAuthenticator*)));
}

ownCloudInfo::~ownCloudInfo()
{
    delete _manager;
    delete _sslErrorDialog;
}

bool ownCloudInfo::isConfigured()
{
    MirallConfigFile cfgFile;
    return cfgFile.connectionExists( _connection );
}

void ownCloudInfo::checkInstallation()
{
    getRequest( "status.php", false );
}

void ownCloudInfo::getWebDAVPath( const QString& path )
{
    getRequest( path, true );
}

void ownCloudInfo::getRequest( const QString& path, bool webdav )
{
    qDebug() << "Get Request to " << path;

    MirallConfigFile cfgFile;
    QString url = cfgFile.ownCloudUrl( _connection, webdav ) + path;
    QNetworkRequest request;
    request.setUrl( QUrl( url ) );
    setupHeaders( request, 0 );

    QNetworkReply *reply = _manager->get( request );
    connect( reply, SIGNAL(finished()), SLOT(slotReplyFinished()));
    _directories[reply] = path;

    connect( reply, SIGNAL( error(QNetworkReply::NetworkError )),
             this, SLOT(slotError( QNetworkReply::NetworkError )));

}

void ownCloudInfo::mkdirRequest( const QString& dir )
{
    qDebug() << "OCInfo Making dir " << dir;

    MirallConfigFile cfgFile;
    QNetworkRequest req;
    req.setUrl( QUrl( cfgFile.ownCloudUrl( _connection, true ) + dir ) );
    QNetworkReply *reply = davRequest("MKCOL", req, 0);

    connect( reply, SIGNAL(finished()), SLOT(slotMkdirFinished()) );
    connect( reply, SIGNAL( error(QNetworkReply::NetworkError )),
             this, SLOT(slotError(QNetworkReply::NetworkError )));
}

void ownCloudInfo::slotMkdirFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());

    if( ! reply ) {
        qDebug() << "ownCloudInfo: Reply empty!";
        return;
    }

    emit webdavColCreated( reply );
    qDebug() << "mkdir slot hit.";
    reply->deleteLater();
}


void ownCloudInfo::slotAuthentication( QNetworkReply*, QAuthenticator *auth )
{
    if( auth ) {
        MirallConfigFile cfgFile;
        qDebug() << "Authenticating request!";
        auth->setUser( cfgFile.ownCloudUser( _connection ) );
        auth->setPassword( cfgFile.ownCloudPasswd( _connection ));
    }
}

void ownCloudInfo::slotSSLFailed( QNetworkReply *reply, QList<QSslError> errors )
{
    qDebug() << "SSL-Warnings happened for url " << reply->url().toString();

    if( _certsUntrusted ) {
        // User decided once to untrust. Honor this decision.
        return;
    }

    if( _sslErrorDialog == 0 ) {
        _sslErrorDialog = new SslErrorDialog();
    }

    if( _sslErrorDialog->setErrorList( errors ) ) {
        // all ssl certs are known and accepted. We can ignore the problems right away.
        qDebug() << "Certs are already known and trusted, Warnings are not valid.";
        reply->ignoreSslErrors();
    } else {
        if( _sslErrorDialog->exec() == QDialog::Accepted ) {
            if( _sslErrorDialog->trustConnection() ) {
                reply->ignoreSslErrors();
            } else {
                // User does not want to trust.
                _certsUntrusted = true;
            }
        }
    }
}

//
// There have been problems with the finish-signal coming from the networkmanager.
// To avoid that, the reply-signals were connected and the data is taken from the
// sender() method.
//
void ownCloudInfo::slotReplyFinished()
{
    QNetworkReply *reply = qobject_cast<QNetworkReply *>(sender());

    if( ! reply ) {
        qDebug() << "ownCloudInfo: Reply empty!";
        return;
    }

    const QString version( reply->readAll() );
    const QString url = reply->url().toString();
    QString plainUrl(url);
    plainUrl.remove("/status.php");

    QString info( version );

    if( url.endsWith("status.php") ) {
        // it was a call to status.php
        if( reply->error() == QNetworkReply::NoError && info.isEmpty() ) {
            // This seems to be a bit strange behaviour of QNetworkAccessManager.
            // It calls the finised slot multiple times but only the first read wins.
            // That happend when the code connected the finished signal of the manager.
            // It did not happen when the code connected to the reply finish signal.
            qDebug() << "WRN: NetworkReply with not content but also no error! " << reply;
            reply->deleteLater();
            return;
        }
        qDebug() << "status.php returns: " << info << " " << reply->error() << " Reply: " << reply;
        if( info.contains("installed") && info.contains("version") && info.contains("versionstring") ) {
            info.remove(0,1); // remove first char which is a "{"
            info.remove(-1,1); // remove the last char which is a "}"
            QStringList li = info.split( QChar(',') );

            QString infoString;
            QString versionStr;
            foreach ( infoString, li ) {
                if( infoString.contains( "versionstring") ) {
                    // get the version string out.
                    versionStr = infoString.mid(17);
                    versionStr.remove(-1, 1);
                }
            }
            emit ownCloudInfoFound( plainUrl, versionStr );
        } else {
            qDebug() << "No proper answer on " << url;
            emit noOwncloudFound( reply );
        }
    } else {
        // it was a general GET request.
        QString dir("unknown");
        if( _directories.contains(reply) ) {
            dir = _directories[reply];
            _directories.remove(reply);
        }

        emit ownCloudDirExists( dir, reply );
    }
    reply->deleteLater();
}

void ownCloudInfo::resetSSLUntrust()
{
    _certsUntrusted = false;
}

void ownCloudInfo::slotError( QNetworkReply::NetworkError err)
{
  qDebug() << "ownCloudInfo Network Error: " << err;
}

// ============================================================================
void ownCloudInfo::setupHeaders( QNetworkRequest & req, quint64 size )
{
    MirallConfigFile cfgFile;

    QUrl url( cfgFile.ownCloudUrl( QString(), false ) );
    qDebug() << "Setting up host header: " << url.host();
    req.setRawHeader( QByteArray("Host"), url.host().toUtf8() );
    req.setRawHeader( QByteArray("User-Agent"), QString("mirall-%1").arg(MIRALL_STRINGIFY(MIRALL_VERSION)).toAscii());
    req.setRawHeader( QByteArray("Authorization"), cfgFile.basicAuthHeader() );

    if (size) {
        req.setHeader( QNetworkRequest::ContentLengthHeader, QVariant(size));
        req.setHeader( QNetworkRequest::ContentTypeHeader, QVariant("text/xml; charset=utf-8"));
    }
}

QNetworkReply* ownCloudInfo::davRequest(const QString& reqVerb,  QNetworkRequest& req, QByteArray *data)
{
    MirallConfigFile cfgFile;

    setupHeaders(req, quint64(data ? data->size() : 0));
    if( data ) {
        QBuffer iobuf( data );
        return _manager->sendCustomRequest(req, reqVerb.toUtf8(), &iobuf );
    } else {
        return _manager->sendCustomRequest(req, reqVerb.toUtf8(), 0 );
    }

}


}

