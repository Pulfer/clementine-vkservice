#include <math.h>

#include <QMenu>
#include <QSettings>
#include <QByteArray>

#include <boost/scoped_ptr.hpp>

#include "core/application.h"
#include "core/logging.h"
#include "core/timeconstants.h"
#include "ui/iconloader.h"

#include "internetmodel.h"
#include "internetplaylistitem.h"
#include "globalsearch/globalsearch.h"

#include "vreen/auth/oauthconnection.h"
#include "vreen/audio.h"
#include "vreen/contact.h"
#include "vreen/roster.h"

#include "globalsearch/vksearchprovider.h"
#include "vkservice.h"

#define  __(var) qLog(Debug) << #var " =" << (var);

const char*  VkService::kServiceName = "Vk.com";
const char*  VkService::kSettingGroup = "Vk.com";
const uint   VkService::kApiKey = 3421812;
const Scopes VkService::kScopes =
        Vreen::OAuthConnection::Offline |
        Vreen::OAuthConnection::Audio |
        Vreen::OAuthConnection::Friends |
        Vreen::OAuthConnection::Groups;


VkService::VkService(Application *app, InternetModel *parent) :
    InternetService(kServiceName, app, parent, parent),
    need_login_(NULL),
    root_item_(NULL),
    recommendations_(NULL),
    my_music_(NULL),
    context_menu_(new QMenu),
    client_(new Vreen::Client),
    connection_(NULL),
    hasAccount_(false),
    provider_(NULL)
{
    QSettings s;
    s.beginGroup(kSettingGroup);

    /* Init connection */
    provider_ = new Vreen::AudioProvider(client_);

    client_->setTrackMessages(false);
    client_->setInvisible(true);

    QByteArray token = s.value("token",QByteArray()).toByteArray();
    int uid = s.value("uid",0).toInt();
    hasAccount_ = not (!uid or token.isEmpty());

    if (hasAccount_) {
        Login();
    };

    connect(client_, SIGNAL(onlineStateChanged(bool)),
            SLOT(OnlineStateChanged(bool)));
    connect(client_, SIGNAL(error(Vreen::Client::Error)),
            SLOT(Error(Vreen::Client::Error)));

    /* Init interface */
    context_menu_->addActions(GetPlaylistActions());
    context_menu_->addAction(IconLoader::Load("configure"), tr("Configure Vk.com..."),
                             this, SLOT(ShowConfig()));

    VkSearchProvider* search_provider = new VkSearchProvider(app_, this);
    search_provider->Init(this);
    app_->global_search()->AddProvider(search_provider);
}

VkService::~VkService()
{
}


QStandardItem *VkService::CreateRootItem()
{
    root_item_ = new QStandardItem(QIcon(":providers/vk.png"),kServiceName);
    root_item_->setData(true, InternetModel::Role_CanLazyLoad);
    return root_item_;
}

void VkService::LazyPopulate(QStandardItem *parent)
{
    switch (parent->data(InternetModel::Role_Type).toInt()) {
    case InternetModel::Type_Service:
        RefreshRootSubitems();
        break;
    case Type_MyMusic: {
        qDebug() << "Load My Music";
        LoadMyMusic();
    }
    default:
        break;
    }
}

void VkService::ShowContextMenu(const QPoint &global_pos)
{
    const bool playable = model()->IsPlayable(model()->current_index());
    GetAppendToPlaylistAction()->setEnabled(playable);
    GetReplacePlaylistAction()->setEnabled(playable);
    GetOpenInNewPlaylistAction()->setEnabled(playable);
    context_menu_->popup(global_pos);
}

void VkService::ItemDoubleClicked(QStandardItem *item)
{
    if (item == need_login_) {
        ShowConfig();
    }
}

void VkService::RefreshRootSubitems()
{
    ClearStandartItem(root_item_);

    recommendations_ = NULL;
    my_music_ = NULL;
    need_login_ = NULL;

    if (hasAccount_) {
        recommendations_ = new QStandardItem(
                    QIcon(":vk/recommends.png"),
                    tr("My Recommendations"));
        recommendations_->setData(Type_Recommendations, InternetModel::Role_Type);
        root_item_->appendRow(recommendations_);

        my_music_ = new QStandardItem(
                    QIcon(":vk/my_music.png"),
                    tr("My Music"));
        my_music_->setData(Type_MyMusic, InternetModel::Role_Type);
        my_music_->setData(true, InternetModel::Role_CanLazyLoad);
        my_music_->setData(InternetModel::PlayBehaviour_MultipleItems,
                           InternetModel::Role_PlayBehaviour);
        root_item_->appendRow(my_music_);

        loading_ = new QStandardItem(
                    QIcon(),
                    tr("Loading...")
                    );
        loading_->setData(Type_Loading, InternetModel::Role_Type);
        qDebug() << "size" << sizeof(*loading_);
    } else {
        need_login_ = new QStandardItem(
                    QIcon(),
                    tr("Double click to login")
                    );
        need_login_->setData(Type_NeedLogin, InternetModel::Role_Type);
        need_login_->setData(InternetModel::PlayBehaviour_DoubleClickAction,
                             InternetModel::Role_PlayBehaviour);
        root_item_->appendRow(need_login_);
    }
}

void VkService::Login()
{
    qLog(Debug) << "--- Login";

    if (connection_) {
        client_->connectToHost();
        emit LoginSuccess(true);
        if (client_->me()) {
            ChangeMe(client_->me());
        }
    } else {
        connection_ = new Vreen::OAuthConnection(kApiKey,client_);
        connection_->setConnectionOption(Vreen::Connection::ShowAuthDialog,true);
        connection_->setScopes(kScopes);
        client_->setConnection(connection_);

        connect(connection_, SIGNAL(accessTokenChanged(QByteArray,time_t)),
                SLOT(ChangeAccessToken(QByteArray,time_t)));
        connect(client_->roster(), SIGNAL(uidChanged(int)),
                SLOT(ChangeUid(int)));
    }

    if (hasAccount_) {
        qLog(Debug) << "--- Have account";

        QSettings s;
        s.beginGroup(kSettingGroup);
        QByteArray token = s.value("token",QByteArray()).toByteArray();
        time_t expiresIn = s.value("expiresIn", 0).toUInt();
        int uid = s.value("uid",0).toInt();

        connection_->setAccessToken(token, expiresIn);
        connection_->setUid(uid);
    }

    client_->connectToHost();
}

void VkService::Logout()
{
    qLog(Debug) << "--- Logout";

    QSettings s;
    s.beginGroup(kSettingGroup);
    s.setValue("token", QByteArray());
    s.setValue("expiresIn",0);
    s.setValue("uid",uint(0));

    hasAccount_ = false;

    if (connection_) {
        client_->disconnectFromHost();
        connection_->clear();
        delete connection_;
        delete client_->roster();
        delete client_->me();
        connection_ = NULL;
    }

    RefreshRootSubitems();
}

uint VkService::SongSearch(const QString &query)
{
    return 0;
}

uint VkService::GroupSearch(const QString &query)
{
    return 0;
}

void VkService::ShowConfig()
{
    app_->OpenSettingsDialogAtPage(SettingsDialog::Page_Vk);
}

void VkService::ChangeAccessToken(const QByteArray &token, time_t expiresIn)
{
    qLog(Debug) << "--- Access token changed";
    QSettings s;
    s.beginGroup(kSettingGroup);
    s.setValue("token", token);
    s.setValue("expiresIn",uint(expiresIn));
}

void VkService::ChangeUid(int uid)
{
    QSettings s;
    s.beginGroup(kSettingGroup);
    s.setValue("uid", uid);
}

void VkService::OnlineStateChanged(bool online)
{
    qLog(Debug) << "--- Online state changed to" << online;
    if (online) {
        hasAccount_ = true;
        emit LoginSuccess(true);
        RefreshRootSubitems();
        connect(client_, SIGNAL(meChanged(Vreen::Buddy*)),
                SLOT(ChangeMe(Vreen::Buddy*)));
    }
}

void VkService::ChangeMe(Vreen::Buddy *me)
{
    if (!me) {
        qLog(Warning) << "Me is NULL.";
        return;
    }

    emit NameUpdated(me->name());
    connect(me, SIGNAL(nameChanged(QString)),
            SIGNAL(NameUpdated(QString)));
    me->update(QStringList("name"));
}

void VkService::Error(Vreen::Client::Error error)
{
    QString msg;

    switch (error) {
    case Vreen::Client::ErrorApplicationDisabled:
        msg = "Application disabled";  break;
    case Vreen::Client::ErrorIncorrectSignature:
        msg = "Incorrect signature";  break;
    case Vreen::Client::ErrorAuthorizationFailed:
        msg = "Authorization failed";
        emit LoginSuccess(false);
        break;
    case Vreen::Client::ErrorToManyRequests:
        msg = "To many requests";  break;
    case Vreen::Client::ErrorPermissionDenied:
        msg = "Permission denied";  break;
    case Vreen::Client::ErrorCaptchaNeeded:
        msg = "Captcha needed";  break;
    case Vreen::Client::ErrorMissingOrInvalidParameter:
        msg = "Missing or invalid parameter";  break;
    case Vreen::Client::ErrorNetworkReply:
        msg = "Network reply";  break;
    default:
        msg = "Unknown error";
        break;
    }

    qLog(Error) << "Client error: " << error << msg;
}

void VkService::LoadMyMusic()
{
    ClearStandartItem(my_music_);
    my_music_->appendRow(loading_);

    auto countOfMyAudio = provider_->getCount();
    connect(countOfMyAudio, SIGNAL(resultReady(QVariant)),
                            SLOT(MyAudioCountRecived()));
}

void VkService::MyAudioCountRecived()
{
    int count = static_cast<Vreen::IntReply*>(sender())->result();
    auto myAudio = provider_->getContactAudio(0,count);
    connect(myAudio, SIGNAL(resultReady(QVariant)),
                    SLOT(MyMusicRecived()));
}

void VkService::MyMusicRecived()
{
    auto reply = static_cast<Vreen::AudioItemListReply*>(sender());
    SongList songs = FromAudioList(reply->result());

    ClearStandartItem(my_music_);
    foreach (const Song& song, songs) {
        QStandardItem* child = CreateSongItem(song);
        child->setData(true, InternetModel::Role_CanBeModified);

        my_music_->appendRow(child);
    }
}

void VkService::SongSearchFinished(int id)
{
}

void VkService::GroupSearchFinished(int id)
{
}

SongList VkService::FromAudioList(const Vreen::AudioItemList &list)
{
    Song song;
    SongList song_list;
    foreach (Vreen::AudioItem item, list) {
        song.set_valid(true);
        song.set_title(item.title().trimmed());
        song.set_artist(item.artist());
        song.set_length_nanosec(floor(item.duration() * kNsecPerSec));
        song.set_url(item.url());

        song_list.append(song);
    }
    return song_list;
}


void VkService::ClearStandartItem(QStandardItem * item)
{
    if (item->hasChildren()) {
        item->removeRows(0, item->rowCount());
    }
}
