/********************************************************************
    Copyright (c) 2013-2015 - Mogara

    This file is part of Cardirector.

    This game engine is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 3.0
    of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

    See the LICENSE file for more details.

    Mogara
*********************************************************************/

#include "cabstractgamelogic.h"
#include "cpacketrouter.h"
#include "cprotocol.h"
#include "croom.h"
#include "cserver.h"
#include "cserveruser.h"
#include "ctcpsocket.h"

#include <QCoreApplication>
#include <QDateTime>

static QHash<int, CPacketRouter::Callback> interactions;
static QHash<int, CPacketRouter::Callback> callbacks;

class CServerUserPrivate
{
public:
    CPacketRouter *router;

    int requestCommand;
    QVariant requestData;

    int networkDelayTestId;
    QDateTime networkDelayStartTime;
};

CServerUser::CServerUser(CTcpSocket *socket, CServer *server)
    : CAbstractServerUser(server)
{
    p_ptr = new CServerUserPrivate;

    p_ptr->router = new CPacketRouter(this, socket, server->packetParser());
    p_ptr->router->setInteractions(&interactions);
    p_ptr->router->setCallbacks(&callbacks);
    connect(p_ptr->router, &CPacketRouter::unknownPacket, this, &CServerUser::handleUnknownPacket);
    connect(p_ptr->router, &CPacketRouter::replyReady, this, &CServerUser::replyReady);
    connect(socket, &CTcpSocket::disconnected, this, &CServerUser::disconnected);

    p_ptr->networkDelayTestId = 0;
}

CServerUser::~CServerUser()
{
    delete p_ptr;
}

void CServerUser::setSocket(CTcpSocket *socket)
{
    p_ptr->router->setSocket(socket);
}

void CServerUser::signup(const QString &username, const QString &password, const QString &screenName, const QString &avatar)
{
    //@to-do: check if the username is duplicated in the database.
    //@to-do: encrypt the password
    static uint userId = 0;
    userId++;
    setId(userId);

    setScreenName(screenName);
    setAvatar(avatar);

    login(username, password);
}

void CServerUser::login(const QString &username, const QString &password)
{
    //@todo: implement this after the database is ready
    C_UNUSED(username);
    C_UNUSED(password);

    notify(S_COMMAND_LOGIN, briefIntroduction());
    setState(Online);
}

void CServerUser::logout()
{
    setState(Invalid);
    p_ptr->router->socket()->disconnectFromHost();
}

void CServerUser::kick()
{
    //@to-do: send a warning
    logout();
}

QHostAddress CServerUser::ip() const
{
    return p_ptr->router->socket()->peerAddress();
}

void CServerUser::updateNetworkDelay()
{
    p_ptr->networkDelayTestId = qrand();
    p_ptr->networkDelayStartTime = QDateTime::currentDateTime();
    notify(S_COMMAND_NETWORK_DELAY, p_ptr->networkDelayTestId);
}

void CServerUser::request(int command, const QVariant &data, int timeout)
{
    p_ptr->router->request(command, data, timeout);
}

void CServerUser::reply(int command, const QVariant &data)
{
    p_ptr->router->reply(command, data);
}

void CServerUser::notify(int command, const QVariant &data)
{
    p_ptr->router->notify(command, data);
}

void CServerUser::prepareRequest(int command, const QVariant &data)
{
    p_ptr->requestCommand = command;
    p_ptr->requestData = data;
}

void CServerUser::executeRequest(int timeout)
{
    request(p_ptr->requestCommand, p_ptr->requestData, timeout);
}

void CServerUser::cancelRequest()
{
    p_ptr->router->cancelRequest();
}

QVariant CServerUser::waitForReply()
{
    return p_ptr->router->waitForReply();
}

QVariant CServerUser::waitForReply(int timeout)
{
    return p_ptr->router->waitForReply(timeout);
}

void CServerUser::AddInteraction(int command, void (*callback)(QObject *, const QVariant &))
{
    interactions.insert(command, callback);
}

void CServerUser::AddCallback(int command, void (*callback)(QObject *, const QVariant &))
{
    callbacks.insert(command, callback);
}

/* Callbacks */

void CServerUser::CheckVersionCommand(QObject *receiver, const QVariant &data)
{
    C_UNUSED(receiver);
    C_UNUSED(data);
}

void CServerUser::SignupCommand(QObject *receiver, const QVariant &data)
{
    QVariantList arguments(data.toList());
    if (arguments.length() < 4)
        return;

    QString account = arguments.at(0).toString();
    QString password = arguments.at(1).toString();
    QString screenName = arguments.at(2).toString();
    QString avatar = arguments.at(3).toString();

    CServerUser *user = qobject_cast<CServerUser *>(receiver);
    user->signup(account, password, screenName, avatar);
}

//currently unused
void CServerUser::LoginCommand(QObject *receiver, const QVariant &data)
{
    C_UNUSED(receiver);
    C_UNUSED(data);

    /*QVariantList dataList(data.toList());
    if (dataList.size() >= 2) {
        QString account = dataList.at(0).toString();
        QString password = dataList.at(1).toString();

        //@to-do: implement this after database is ready
        CServerUser *user = qobject_cast<CServerUser *>(receiver);
        user->login(account, password);
    }*/
}

void CServerUser::LogoutCommand(QObject *receiver, const QVariant &)
{
    CServerUser *user = qobject_cast<CServerUser *>(receiver);
    user->logout();
}

void CServerUser::SpeakCommand(QObject *receiver, const QVariant &data)
{
    CServerUser *user = qobject_cast<CServerUser *>(receiver);
    QString message = data.toString();
    if (!message.isEmpty())
        user->speak(message);
}

void CServerUser::CreateRoomCommand(QObject *receiver, const QVariant &data)
{
    CServerUser *user = qobject_cast<CServerUser *>(receiver);
    CServer *server = user->server();

    QVariantMap config = data.toMap();
    QString name = config.value("name").toString();
    uint capacity = config.value("capacity", 0).toUInt();

    server->createRoom(user, name, capacity);
}

void CServerUser::EnterRoomCommand(QObject *receiver, const QVariant &data)
{
    CServerUser *user = qobject_cast<CServerUser *>(receiver);
    CServer *server = user->server();

    if (data.isNull()) {
        CRoom *lobby = server->lobby();
        lobby->addUser(user);
    } else {
        uint roomId = data.toUInt();
        CRoom *room = server->findRoom(roomId);
        if (room)
            room->addUser(user);
    }
}

void CServerUser::NetworkDelayCommand(QObject *receiver, const QVariant &data)
{
    CServerUser *user = qobject_cast<CServerUser *>(receiver);
    CServerUserPrivate *p_ptr = user->p_ptr;
    if (p_ptr->networkDelayTestId != 0 && p_ptr->networkDelayTestId == data.toInt()) {
        user->setNetworkDelay(p_ptr->networkDelayStartTime.secsTo(QDateTime::currentDateTime()));
        p_ptr->networkDelayTestId = 0;
    }
}

void CServerUser::SetRoomListCommand(QObject *receiver, const QVariant &)
{
    CServerUser *user = qobject_cast<CServerUser *>(receiver);
    CServer *server = user->server();
    server->updateRoomList(user);
}

void CServerUser::GameStartCommand(QObject *receiver, const QVariant &)
{
    CServerUser *user = qobject_cast<CServerUser *>(receiver);
    CRoom *room = user->room();
    CAbstractGameLogic *gameLogic = room->gameLogic();
    if (gameLogic && !gameLogic->isRunning())
        gameLogic->start();
}

void CServerUser::AddRobotCommand(QObject *receiver, const QVariant &)
{
    CServerUser *user = qobject_cast<CServerUser *>(receiver);
    CRoom *room = user->room();
    if (!room->isFull()) {
        CServer *server = user->server();
        server->createRobot(room);
    }
}

void CServerUser::handleUnknownPacket(const QByteArray &packet)
{
    //Handle requests from a browser
    if (packet.startsWith("GET") || packet.startsWith("POST")) {
        CTcpSocket *socket = p_ptr->router->socket();

        //Read the whole HTTP request
        while (socket->canReadLine()) {
            QByteArray line = socket->readLine();
            if (line.isEmpty() || line.at(0) == '\n' || line.at(0) == '\r')
                break;
        }

        QString scheme = qApp->applicationName();
        scheme.remove(QRegExp("[^A-Za-z]"));
        QString location = QString("Location: %1://%2:%3/\r\n");
        location = location.arg(scheme).arg(socket->localAddress().toString()).arg(socket->localPort());

        socket->write("HTTP/1.1 302 Moved Temporarily\r\n");
        socket->write("Server: Cardirector\r\n");
        socket->write(location.toLatin1());
        socket->write("Connection: close\r\n");
        socket->write("\r\n");

        socket->disconnectFromHost();
    }
}

void CServerUser::Init()
{
    AddCallback(S_COMMAND_CHECK_VERSION, &CheckVersionCommand);
    AddCallback(S_COMMAND_SIGNUP, &SignupCommand);
    AddCallback(S_COMMAND_LOGIN, &LoginCommand);
    AddCallback(S_COMMAND_LOGOUT, &LogoutCommand);
    AddCallback(S_COMMAND_SPEAK, &SpeakCommand);
    AddCallback(S_COMMAND_CREATE_ROOM, &CreateRoomCommand);
    AddCallback(S_COMMAND_ENTER_ROOM, &EnterRoomCommand);
    AddCallback(S_COMMAND_NETWORK_DELAY, &NetworkDelayCommand);
    AddCallback(S_COMMAND_SET_ROOM_LIST, &SetRoomListCommand);
    AddCallback(S_COMMAND_GAME_START, &GameStartCommand);
    AddCallback(S_COMMAND_ADD_ROBOT, &AddRobotCommand);
}
C_INITIALIZE_CLASS(CServerUser)