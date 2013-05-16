#include <math.h>

#include <QApplication>
#include <QByteArray>
#include <QClipboard>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QMenu>
#include <QSettings>
#include <QTimer>
#include <QTemporaryFile>

#include <boost/scoped_ptr.hpp>

#include "core/application.h"
#include "core/closure.h"
#include "core/logging.h"
#include "core/mergedproxymodel.h"
#include "core/player.h"
#include "core/timeconstants.h"
#include "core/taskmanager.h"
#include "ui/iconloader.h"
#include "widgets/didyoumean.h"

#include "globalsearch/globalsearch.h"
#include "internetmodel.h"
#include "internetplaylistitem.h"
#include "searchboxwidget.h"

#include "vreen/audio.h"
#include "vreen/auth/oauthconnection.h"
#include "vreen/contact.h"
#include "vreen/roster.h"

#include "globalsearch/vksearchprovider.h"
#include "vkservice.h"

const char*  VkService::kServiceName = "Vk.com";
const char*  VkService::kSettingGroup = "Vk.com";
const char*  VkService::kUrlScheme = "vk";
const uint   VkService::kApiKey = 3421812;
const Scopes VkService::kScopes =
        Vreen::OAuthConnection::Offline |
        Vreen::OAuthConnection::Audio |
        Vreen::OAuthConnection::Friends |
        Vreen::OAuthConnection::Groups;

const char* VkService::kDefCacheFilename = "%artist - %title";
QString     VkService::kDefCacheDir() {  return QDir::homePath()+"/Vk Cache";}

const int VkService::kMaxVkSongList = 6000;

uint VkService::RequestID::last_id_ = 0;



/***
 * Little functions
 */

inline static void RemoveLastRow(QStandardItem* item){
    item->removeRow(item->rowCount() - 1);
}

struct SongId {
    SongId()
        : audio_id(0),
          owner_id(0)
    {}

    int audio_id;
    int owner_id;
};

static SongId ExtractIds(const QUrl &url) {
    QString str = url.toString();
    if (str.startsWith("vk://song/")) {
        QStringList ids =
                str.remove("vk://song/")    // "<oid>_<aid>/<artist>/<title>"
                .section('/',0,0)           // "<oid>_<aid>
                .split('_');                // {"<oid>","<aid>"}

        if (ids.count() < 2) {
            qLog(Warning) << "Wrong song url" << url;
            return SongId();
        }
        SongId res;
        res.owner_id = ids[0].toInt();
        res.audio_id = ids[1].toInt();
        return res;
    } else {
        qLog(Error) << "Wromg song url" << url;
    }
    return SongId();
}

static Song SongFromUrl(QUrl url) {
    QString str = url.toString();
    Song result;
    if (str.startsWith("vk://song/")) {
        QStringList ids = str.remove("vk://song/").split('/');
        if (ids.size() == 3) {
            result.set_artist(ids[1]);
            result.set_title(ids[2]);
        }
        result.set_url(url);
    } else {
        qLog(Error) << "Wromg song url" << url;
    }
    return result;
}



/*******************************************************************************
 *
 * MusicOwner
 *
 *******************************************************************************/

VkService::MusicOwner::MusicOwner(const Song &owner_radio) {
    QStringList tokens = owner_radio.url().toString().remove("vk://").split('/');
    id_ = -tokens[1].toInt();
    name_ = owner_radio.comment();
    songs_count_ = tokens[2].toInt();
    screen_name_ = tokens[3];
}

Song VkService::MusicOwner::toOwnerRadio() const {
    Song song;
    song.set_title(tr("%1 (%0 songs)").arg(songs_count_).arg(name_));
    song.set_comment(name_);
    song.set_url(QUrl(QString("vk://group/%1/%2/%3").arg(-id_).arg(songs_count_)
                      .arg(screen_name_)));
    song.set_artist(tr(" Group radio"));
    return song;
}

QDataStream &operator <<(QDataStream &stream, const VkService::MusicOwner &val)
{
    stream << val.id_;
    stream << val.name_;
    stream << val.songs_count_;
    stream << val.screen_name_;
    return stream;
}

QDataStream &operator >>(QDataStream &stream, VkService::MusicOwner &val)
{
    stream >> val.id_;
    stream >> val.name_;
    stream >> val.songs_count_;
    stream >> val.screen_name_;
    return stream;
}

QDebug operator <<(QDebug d, const VkService::MusicOwner &owner)
{
    d << "MusicOwner("
      << owner.id_ << ","
      << owner.name_ << ","
      << owner.songs_count_ << ","
      << owner.screen_name_ << ")";
    return d;
}





/*****************************************************************************
 *
 * VkService realisation
 *
 *****************************************************************************/

VkService::VkService(Application *app, InternetModel *parent) :
    InternetService(kServiceName, app, parent, parent),
    root_item_(nullptr),
    recommendations_(nullptr),
    my_music_(nullptr),
    search_(nullptr),
    context_menu_(nullptr),
    update_my_music_(nullptr),
    update_recommendations_(nullptr),
    find_this_artist_(nullptr),
    add_to_my_music_(nullptr),
    remove_from_my_music_(nullptr),
    search_box_(new SearchBoxWidget(this)),
    client_(new Vreen::Client),
    connection_(nullptr),
    hasAccount_(false),
    my_id_(0),
    url_handler_(new VkUrlHandler(this, this)),
    audio_provider_(nullptr),
    cache_(new VkMusicCache(this)),
    last_search_id_(0)
{
    QSettings s;
    s.beginGroup(kSettingGroup);

    /* Init connection */
    audio_provider_ = new Vreen::AudioProvider(client_);

    client_->setTrackMessages(false);
    client_->setInvisible(true);

    QByteArray token = s.value("token",QByteArray()).toByteArray();
    my_id_ = s.value("uid",0).toInt();
    hasAccount_ = (my_id_ != 0) and !token.isEmpty();

    if (hasAccount_) {
        Login();
    };

    connect(client_, SIGNAL(onlineStateChanged(bool)),
            SLOT(OnlineStateChanged(bool)));
    connect(client_, SIGNAL(error(Vreen::Client::Error)),
            SLOT(Error(Vreen::Client::Error)));

    /* Init interface */
    CreateMenu();

    VkSearchProvider* search_provider = new VkSearchProvider(app_, this);
    search_provider->Init(this);
    app_->global_search()->AddProvider(search_provider);
    connect(search_box_, SIGNAL(TextChanged(QString)), SLOT(SearchSongs(QString)));

    app_->player()->RegisterUrlHandler(url_handler_);

    UpdateSettings();
}

VkService::~VkService()
{
}



/***
 * Interface
 */

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
    case Type_MyMusic:
        UpdateMyMusic();
        break;
    case Type_Recommendations:
        UpdateRecommendations();
        break;
    case Type_Bookmark:
        break;

    default:
        break;
    }
}

void VkService::CreateMenu()
{
    context_menu_ = new QMenu;

    context_menu_->addActions(GetPlaylistActions());
    context_menu_->addSeparator();

    add_to_bookmarks_ = context_menu_->addAction(
                QIcon(":vk/add.png"), tr("Add to bookmarks"),
                this, SLOT(AddToBookmarks()));

    remove_from_bookmarks_ = context_menu_->addAction(
                QIcon(":vk/remove.png"), tr("Remove from bookmarks"),
                this, SLOT(RemoveFromBookmark()));

    context_menu_->addSeparator();
    update_my_music_ = context_menu_->addAction(
                IconLoader::Load("view-refresh"), tr("Update My Music"),
                this, SLOT(UpdateMyMusic()));
    update_recommendations_ = context_menu_->addAction(
                IconLoader::Load("view-refresh"), tr("Update Recommendations"),
                this, SLOT(UpdateRecommendations()));

    find_this_artist_ = context_menu_->addAction(
                QIcon(":vk/find.png"), tr("Find this artist"),
                this, SLOT(FindThisArtist()));

    add_to_my_music_ = context_menu_->addAction(
                QIcon(":vk/add.png"), tr("Add to My Music"),
                this, SLOT(AddToMyMusic()));

    remove_from_my_music_ = context_menu_->addAction(
                QIcon(":vk/remove.png"), tr("Remove from My Music"),
                this, SLOT(RemoveFromMyMusic()));

    add_song_to_cache_ = context_menu_->addAction(
                QIcon(":vk/download.png"), tr("Add song to cache"),
                this, SLOT(AddToCache()));

    copy_share_url_ = context_menu_->addAction(
                QIcon(":vk/link.png"), tr("Copy share url to clipboard"),
                this, SLOT(CopyShareUrl()));

    context_menu_->addSeparator();
    context_menu_->addAction(
                IconLoader::Load("configure"), tr("Configure Vk.com..."),
                this, SLOT(ShowConfig()));
}

void VkService::ShowContextMenu(const QPoint &global_pos)
{
    QModelIndex current(model()->current_index());

    const int item_type = current.data(InternetModel::Role_Type).toInt();
    const int parent_type = current.parent().data(InternetModel::Role_Type).toInt();

    const bool is_playable = model()->IsPlayable(current);
    const bool is_my_music_item =
            item_type == Type_MyMusic or parent_type == Type_MyMusic;
    const bool is_recommend_item =
            item_type == Type_Recommendations or parent_type == Type_Recommendations;
    const bool is_track =
            item_type == InternetModel::Type_Track;
    const bool is_bookmark_ = item_type == Type_Bookmark;

    bool is_in_mymusic = false;

    if (is_track) {
        selected_song_ = current.data(InternetModel::Role_SongMetadata).value<Song>();
        is_in_mymusic = is_my_music_item or
                ExtractIds(selected_song_.url()).owner_id == my_id_;
    }

    GetAppendToPlaylistAction()->setEnabled(is_playable);
    GetReplacePlaylistAction()->setEnabled(is_playable);
    GetOpenInNewPlaylistAction()->setEnabled(is_playable);

    update_my_music_->setVisible(is_my_music_item);
    update_recommendations_->setVisible(is_recommend_item);
    find_this_artist_->setVisible(is_track);
    add_song_to_cache_->setVisible(is_track);
    add_to_my_music_->setVisible(is_track and not is_in_mymusic);
    remove_from_my_music_->setVisible(is_track and is_in_mymusic);
    copy_share_url_->setVisible(is_track);
    remove_from_bookmarks_->setVisible(is_bookmark_);
    add_to_bookmarks_->setVisible(false);

    context_menu_->popup(global_pos);
}

void VkService::ItemDoubleClicked(QStandardItem *item)
{
    switch (item->data(InternetModel::Role_Type).toInt()) {
    case Type_NeedLogin:
        ShowConfig();
        break;
    case Type_More:
        switch (item->parent()->data(InternetModel::Role_Type).toInt()) {
        case Type_Recommendations:
            MoreRecommendations();
            break;
        case Type_Search:
            MoreSearch();
            break;
        default:
            qLog(Warning) << "Wrong parent for More item:" << item->parent()->text();
        }
        break;
    default:
        qLog(Warning) << "Wrong item for double click with type:" << item->data(InternetModel::Role_Type);
    }
}

QList<QAction *> VkService::playlistitem_actions(const Song &song)
{
    QList<QAction *> actions;
    QString url = song.url().toString();

    if (url.startsWith("vk://song")) {
        selected_song_ = song;
    } else if (url.startsWith("vk://group")) {
        add_to_bookmarks_->setVisible(true);
        actions << add_to_bookmarks_;
        if (song.url() == current_group_url_) {
            // Selected now playing group.
            selected_song_ = current_song_;
            // HACK: Restore name of group in song comment, need to review this way.
            selected_song_.set_comment(song.comment());
        } else {
            // If selected not playing group, return only "Add to bookmarks" action.
            selected_song_ = song;
            return actions;
        }
    }

    // Adding songs actions.
    find_this_artist_->setVisible(true);
    actions << find_this_artist_;

    if (ExtractIds(selected_song_.url()).owner_id != my_id_) {
        add_to_my_music_->setVisible(true);
        actions << add_to_my_music_;
    } else {
        remove_from_my_music_->setVisible(true);
        actions << remove_from_my_music_;
    }

    actions << copy_share_url_;

    if (!cache()->InCache(song.url())) {
        actions << add_song_to_cache_;
    }

    return actions;
}

void VkService::ShowConfig()
{
    app_->OpenSettingsDialogAtPage(SettingsDialog::Page_Vk);
}

void VkService::RefreshRootSubitems()
{
    ClearStandartItem(root_item_);

    if (hasAccount_) {
        CreateAndAppendRow(root_item_, Type_Recommendations);
        CreateAndAppendRow(root_item_, Type_MyMusic);
        LoadBookmarks();
    } else {
        CreateAndAppendRow(root_item_, Type_NeedLogin);
    }
}

QWidget *VkService::HeaderWidget() const
{
    if (HasAccount()) {
        return search_box_;
    } else {
        return nullptr;
    }
}

QStandardItem* VkService::CreateAndAppendRow(QStandardItem *parent, VkService::ItemType type){

    QStandardItem* item = nullptr;

    switch (type) {
    case Type_NeedLogin:
        item = new QStandardItem(
                    QIcon(),
                    tr("Double click to login")
                    );
        item->setData(InternetModel::PlayBehaviour_DoubleClickAction,
                             InternetModel::Role_PlayBehaviour);\
        break;
    case Type_Loading:
        item = new QStandardItem(
                    QIcon(),
                    tr("Loading...")
                    );
        break;

    case Type_More:
        item = new QStandardItem(
                    QIcon(),
                    tr("More")
                    );
        item->setData(InternetModel::PlayBehaviour_DoubleClickAction,
                             InternetModel::Role_PlayBehaviour);
        break;

    case Type_Recommendations:
        item = new QStandardItem(
                    QIcon(":vk/recommends.png"),
                    tr("My Recommendations"));
        item->setData(true, InternetModel::Role_CanLazyLoad);
        item->setData(InternetModel::PlayBehaviour_MultipleItems,
                                  InternetModel::Role_PlayBehaviour);
        recommendations_ = item;
        break;

    case Type_MyMusic:
        item = new QStandardItem(
                    QIcon(":vk/my_music.png"),
                    tr("My Music"));
        item->setData(true, InternetModel::Role_CanLazyLoad);
        item->setData(InternetModel::PlayBehaviour_MultipleItems,
                           InternetModel::Role_PlayBehaviour);
        my_music_ = item;
        break;

    case Type_Search:
        item = new QStandardItem(
                    QIcon(":vk/find.png"),
                    tr("Search"));
        item->setData(InternetModel::PlayBehaviour_MultipleItems,
                           InternetModel::Role_PlayBehaviour);
        search_ = item;
        break;

    case Type_Bookmark:
        qLog(Error) << "Use AppendBookmarkFromRadio(QStandardItem *parent, const Song &owner_radio)"
                    << "for creating Bookmark item instead.";
        break;

    default:
        qLog(Error) << "Invalid type for creating" << type;
        break;
    }

    item->setData(type, InternetModel::Role_Type);
    parent->appendRow(item);
    return item;
}



/***
 * Connection
 */

void VkService::Login()
{
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
        connection_ = nullptr;
    }

    RefreshRootSubitems();
}

void VkService::ChangeAccessToken(const QByteArray &token, time_t expiresIn)
{
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
    my_id_ = uid;
}

void VkService::OnlineStateChanged(bool online)
{
    qLog(Debug) << "Online state changed to" << online;
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



/***
 * My Music
 */

void VkService::UpdateMyMusic()
{
    if (not my_music_) {
        // Internet services panel still not created.
        return;
    }

    ClearStandartItem(my_music_);
    CreateAndAppendRow(my_music_,Type_Loading);
    update_my_music_->setEnabled(false);

    LoadSongList(0);

    connect(this, SIGNAL(SongListLoaded(RequestID,SongList)),
            this, SLOT(MyMusicLoaded(RequestID,SongList)));
}

void VkService::MyMusicLoaded(RequestID rid, const SongList &songs)
{
    if(rid.type() == UserAudio and rid.id() == 0) {
        update_my_music_->setEnabled(true);
        disconnect(this, SLOT(MyMusicLoaded(RequestID,SongList)));
        ClearStandartItem(my_music_);
        AppendSongs(my_music_,songs);
    }
}



/***
 * Recommendation
 */

void VkService::UpdateRecommendations()
{
    ClearStandartItem(recommendations_);
    CreateAndAppendRow(recommendations_,Type_Loading);
    update_recommendations_->setEnabled(false);

    auto myAudio = audio_provider_->getRecommendationsForUser(0,50,0);
    NewClosure(myAudio, SIGNAL(resultReady(QVariant)), this,
               SLOT(SongListRecived(RequestID,Vreen::AudioItemListReply*)),
               RequestID(UserRecomendations), myAudio);

    connect(this, SIGNAL(SongListLoaded(RequestID,SongList)),
            this, SLOT(RecommendationsLoaded(RequestID,SongList)));
}

void VkService::MoreRecommendations()
{
    RemoveLastRow(recommendations_); // Last row is "More"
    update_recommendations_->setEnabled(false);
    CreateAndAppendRow(recommendations_,Type_Loading);
    auto myAudio = audio_provider_->getRecommendationsForUser(0,50,recommendations_->rowCount()-1);

    NewClosure(myAudio, SIGNAL(resultReady(QVariant)), this,
               SLOT(SongListRecived(RequestID,Vreen::AudioItemListReply*)),
               RequestID(UserRecomendations), myAudio);

    connect(this, SIGNAL(SongListLoaded(RequestID,SongList)),
            this, SLOT(RecommendationsLoaded(RequestID,SongList)));
}

void VkService::RecommendationsLoaded(RequestID id, const SongList &songs)
{
    if(id.type() == UserRecomendations) {
        update_recommendations_->setEnabled(true);
        disconnect(this, SLOT(RecommendationsLoaded(RequestID,SongList)));
        RemoveLastRow(recommendations_); // Last row is "Loading..."
        AppendSongs(recommendations_,songs);
        CreateAndAppendRow(recommendations_,Type_More);
    }
}



/***
 * Features
 */

void VkService::FindThisArtist()
{
    search_box_->SetText(selected_song_.artist());
}

void VkService::AddToMyMusic()
{
    SongId id = ExtractIds(selected_song_.url());
    auto reply = audio_provider_->addToLibrary(id.audio_id,id.owner_id);
    connect(reply, SIGNAL(resultReady(QVariant)),
            this, SLOT(UpdateMyMusic()));
}

void VkService::AddToMyMusicCurrent()
{
    if (isLoveAddToMyMusic()) {
        selected_song_ = current_song_;
        AddToMyMusic();
    }
}

void VkService::RemoveFromMyMusic()
{
    SongId id = ExtractIds(selected_song_.url());
    if (id.owner_id == my_id_) {
        auto reply = audio_provider_->removeFromLibrary(id.audio_id,id.owner_id);
        connect(reply, SIGNAL(resultReady(QVariant)),
                this, SLOT(UpdateMyMusic()));
    } else {
        qLog(Warning) << "You tried to delete not your (" << my_id_
                      << ") song with url" << selected_song_.url();
    }
}

void VkService::AddToCache()
{
    url_handler_->ForceAddToCache(selected_song_.url());
}

void VkService::CopyShareUrl()
{
    QByteArray share_url("http://vk.com/audio?q=");
    share_url += QUrl::toPercentEncoding(
                QString(selected_song_.artist() + " " + selected_song_.title()));

    QApplication::clipboard()->setText(share_url);
}



/***
 * Bookmarks
 */

void VkService::AddToBookmarks()
{
    AppendBookmarkFromRadio(root_item_, selected_song_);
    SaveBookmarks();
}

void VkService::RemoveFromBookmark()
{
    QModelIndex current(model()->current_index());
    root_item_->removeRow(current.row());
    SaveBookmarks();
}

void VkService::SaveBookmarks()
{
    QSettings s;
    s.beginGroup(kSettingGroup);

    s.beginWriteArray("bookmarks");
    int index = 0;
    for (int i = 0; i < root_item_->rowCount(); ++i){
        auto item = root_item_->child(i);
        if (item->data(InternetModel::Role_Type).toInt() == Type_Bookmark){
            Song song = item->data(InternetModel::Role_SongMetadata).value<Song>();
            s.setArrayIndex(index);
            MusicOwner owner(song);
            qLog(Info) << "Save" << index << ":" << owner;
            s.setValue("owner", QVariant::fromValue(owner));
            ++index;
        }
    }
    s.endArray();
}

void VkService::LoadBookmarks()
{
    QSettings s;
    s.beginGroup(kSettingGroup);

    int max = s.beginReadArray("bookmarks");
    for (int i = 0; i < max; ++i){
        s.setArrayIndex(i);
        MusicOwner owner = s.value("owner").value<MusicOwner>();
        qLog(Info) << "Load" << i << ":" << owner;
        AppendBookmarkFromRadio(root_item_, owner.toOwnerRadio());
    }
    s.endArray();
}


QStandardItem* VkService::AppendBookmarkFromRadio(QStandardItem *parent, const Song &owner_radio)
{
    MusicOwner owner(owner_radio);
    QStandardItem *item = new QStandardItem(
                QIcon(":vk/group.png"),
                owner.name());

    item->setData(QVariant::fromValue(owner) ,Role_MusicOwnerMetadata);
    item->setData(Type_Bookmark, InternetModel::Role_Type);
    item->setData(true, InternetModel::Role_CanLazyLoad);
    item->setData(InternetModel::PlayBehaviour_MultipleItems,
                       InternetModel::Role_PlayBehaviour);
    parent->appendRow(item);
    return item;
}




/***
 * Search
 */

void VkService::SearchSongs(QString query)
{
    if (query.isEmpty()) {
        root_item_->removeRow(search_->row());
        search_ = nullptr;
        last_search_id_ = 0;
    } else {
        last_query_ = query;
        if (!search_) {
            CreateAndAppendRow(root_item_,Type_Search);
            connect(this, SIGNAL(SongSearchResult(RequestID,SongList)),
                    SLOT(SearchResultLoaded(RequestID,SongList)));
        }
        RemoveLastRow(search_); // Prevent multiple "Loading..." rows.
        CreateAndAppendRow(search_, Type_Loading);
        SongSearch(RequestID(LocalSearch), query);
    }
}

void VkService::MoreSearch()
{
    RemoveLastRow(search_); // Last row is "More"
    CreateAndAppendRow(recommendations_,Type_Loading);

    RequestID  rid(MoreLocalSearch);
    SongSearch(rid,last_query_,50,search_->rowCount()-1);
}

void VkService::SearchResultLoaded(RequestID rid, const SongList &songs)
{
    if (!search_) {
        return; // Result received when search is already over.
    }

    if (rid.id() >= last_search_id_){
        if (rid.type() == LocalSearch) {
            ClearStandartItem(search_);
        } else if (rid.type() == MoreLocalSearch) {
            RemoveLastRow(search_); // Remove only  "Loading..."
        } else {
            return; // Others request types ignored.
        }

        last_search_id_= rid.id();

        if (songs.count() > 0) {
            AppendSongs(search_, songs);
            CreateAndAppendRow(search_, Type_More);
        }

        // If new search, scroll to search results.
        if (rid.type() == LocalSearch) {
            QModelIndex index = model()->merged_model()->mapFromSource(search_->index());
            ScrollToIndex(index);
        }
    }
}



/***
 * Load song list methods
 */

void VkService::LoadSongList(uint uid, uint count)
{
    if (count > 0) {
        auto myAudio = audio_provider_->getContactAudio(uid,count);
        NewClosure(myAudio, SIGNAL(resultReady(QVariant)), this,
                   SLOT(SongListRecived(RequestID,Vreen::AudioItemListReply*)),
                   RequestID(UserAudio,uid), myAudio);
    } else {
        // If count undefined (count = 0) load all
        auto countOfMyAudio = audio_provider_->getCount(uid);
        NewClosure(countOfMyAudio, SIGNAL(resultReady(QVariant)), this,
                   SLOT(CountRecived(RequestID,Vreen::IntReply*)),
                   RequestID(UserAudio,uid), countOfMyAudio);
    }
}

void VkService::CountRecived(RequestID rid, Vreen::IntReply* reply)
{
    int count = reply->result();
    reply->deleteLater();

    auto myAudio = audio_provider_->getContactAudio(0,count);
    NewClosure(myAudio, SIGNAL(resultReady(QVariant)), this,
               SLOT(SongListRecived(RequestID,Vreen::AudioItemListReply*)),
               rid, myAudio);
}

void VkService::SongListRecived(RequestID rid, Vreen::AudioItemListReply* reply)
{
    SongList songs = FromAudioList(reply->result());
    reply->deleteLater();
    emit SongListLoaded(rid, songs);
}


static QString ClearString(QString str) {
    // Remove all leading and trailing unicode symbols
    // that some users love to add to title and artist.
    str = str.remove(QRegExp("^[^\\w]*"));
    str = str.remove(QRegExp("[^])\\w]*$"));
    return str;
}

Song VkService::FromAudioItem(const Vreen::AudioItem &item)
{
    Song song;
    song.set_title(ClearString(item.title()));
    song.set_artist(ClearString(item.artist()));
    song.set_length_nanosec(floor(item.duration() * kNsecPerSec));

    QString url = QString("vk://song/%1_%2/%3/%4").
            arg(item.ownerId()).
            arg(item.id()).
            arg(item.artist().replace('/','_')).
            arg(item.title().replace('/','_'));

    song.set_url(QUrl(url));
    return song;
}

SongList VkService::FromAudioList(const Vreen::AudioItemList &list)
{
    SongList song_list;
    foreach (Vreen::AudioItem item, list) {
        song_list.append(FromAudioItem(item));
    }
    return song_list;
}



/***
 * Url handling
 */

QUrl VkService::GetSongPlayUrl(const QUrl &url, bool is_playing)
{
    QStringList tokens = url.toString().remove("vk://song/").split('/');

    if (tokens.count() < 2) {
        qLog(Error) << "Wrong song url" << url;
        return QUrl();
    }

    QString song_id =  tokens[0];
    Vreen::AudioItemListReply *song_request = audio_provider_->getAudiosByIds(song_id);

    emit StopWaiting(); // Stop all previous requests.
    bool succ = WaitForReply(song_request);

    if (succ and not song_request->result().isEmpty()) {
         Vreen::AudioItem song = song_request->result()[0];
         if (is_playing) {
             current_song_ = FromAudioItem(song);
             current_song_.set_url(url);
         }
         return song.url();
    } else {
        qLog(Info) << "Unresolved url by id" << song_id;
        return QUrl();
    }
}

UrlHandler::LoadResult VkService::GetGroupNextSongUrl(const QUrl &url)
{
    QStringList tokens = url.toString().remove("vk://group/").split('/');
    if (tokens.count() < 2) {
        qLog(Error) << "Wrong url" << url;
        return UrlHandler::LoadResult();
    }

    int gid = tokens[0].toInt();
    int songs_count = tokens[1].toInt();

    if (songs_count > kMaxVkSongList){
        songs_count = kMaxVkSongList;
    }

    // Getting one random song from groups playlist.
    Vreen::AudioItemListReply* song_request = audio_provider_->getContactAudio(-gid,1,random() % songs_count);

    emit StopWaiting(); // Stop all previous requests.
    bool succ = WaitForReply(song_request);

    if (succ and not song_request->result().isEmpty()) {
         Vreen::AudioItem song = song_request->result()[0];
         current_group_url_ = url;
         current_song_ = FromAudioItem(song);
         emit StreamMetadataFound(url, current_song_);
         return UrlHandler::LoadResult(url,
                                       UrlHandler::LoadResult::TrackAvailable,
                                       song.url(),
                                       current_song_.length_nanosec());
    } else {
        qLog(Info) << "Unresolved group url" << url;
        return UrlHandler::LoadResult();
    }
}

void VkService::SetCurrentSongFromUrl(const QUrl &url)
{
    current_song_ = SongFromUrl(url);
}

/***
 * Search
 */

void VkService::SongSearch(RequestID id, const QString &query, int count, int offset)
{
    auto reply = audio_provider_->searchAudio(query,count,offset,false,Vreen::AudioProvider::SortByPopularity);
    NewClosure(reply, SIGNAL(resultReady(QVariant)), this,
               SLOT(SongSearchRecived(RequestID,Vreen::AudioItemListReply*)),
               id, reply);
}

void VkService::SongSearchRecived(RequestID id, Vreen::AudioItemListReply *reply)
{
    SongList songs = FromAudioList(reply->result());
    reply->deleteLater();
    emit SongSearchResult(id, songs);
}

void VkService::GroupSearch(VkService::RequestID id, const QString &query, int count, int offset)
{
    QVariantMap args;
    args.insert("q", query);

//    This is using of 'execute' method that execute this VKScript method on vk server:
//    var groups = API.groups.search({"q": Args.q});
//    if (groups.length == 0) {
//        return [];
//    }

//    var i = 1;
//    var res = [];
//    while (i < groups.length - 1){
//        i = i + 1;
//        var grp = groups[i];
//        var songs = API.audio.getCount({oid: -grp.gid});
//        if ( songs > 1 &&
//            (grp.is_closed == 0 || grp.is_member == 1))
//        {
//            res = res + [{"songs_count" : songs,
//                            "id" : -grp.gid,
//                            "name" : grp.name,
//                            "screen_name" : grp.screen_name,
//                            "photo": grp.photo}];
//        }
//    }
//    return  res;
//
//    I leave it here just in case if my app will disappear or smth.

    auto reply = client_->request("execute.searchMusicGroup",args);

    NewClosure(reply, SIGNAL(resultReady(QVariant)), this,
               SLOT(GroupSearchRecived(RequestID,Vreen::Reply*)),
               id, reply);
}

void VkService::GroupSearchRecived(VkService::RequestID id, Vreen::Reply *reply)
{
    QVariant groups = reply->response();
    reply->deleteLater();
    emit GroupSearchResult(id, MusicOwner::parseMusicOwnerList(groups));
}




/***
 * Utils
 */

void VkService::AppendSongs(QStandardItem *parent, const SongList &songs)
{
    foreach (auto song, songs) {
        parent->appendRow(CreateSongItem(song));
    }
}

void VkService::UpdateSettings()
{
    QSettings s;
    s.beginGroup(kSettingGroup);
    maxGlobalSearch_ = s.value("max_global_search",50).toInt();
    cachingEnabled_ = s.value("cache_enabled", false).toBool();
    cacheDir_ = s.value("cache_dir",kDefCacheDir()).toString();
    cacheFilename_ = s.value("cache_filename", kDefCacheFilename).toString();
    love_is_add_to_mymusic_ = s.value("love_is_add_to_my_music",false).toBool();
    groups_in_global_search_ = s.value("groups_in_global_search", false).toBool();
}

void VkService::ClearStandartItem(QStandardItem * item)
{
    if (item and item->hasChildren()) {
        item->removeRows(0, item->rowCount());
    }
}

bool VkService::WaitForReply(Vreen::Reply* reply) {
    QEventLoop event_loop;
    QTimer timeout_timer;
    connect(this, SIGNAL(StopWaiting()), &timeout_timer, SLOT(stop()));
    connect(&timeout_timer, SIGNAL(timeout()), &event_loop, SLOT(quit()));
    connect(reply, SIGNAL(resultReady(QVariant)), &event_loop, SLOT(quit()));
    timeout_timer.start(10000);
    event_loop.exec();
    if (!timeout_timer.isActive()) {
      qLog(Error) << "Vk.com request timeout";
      return false;
    }
    timeout_timer.stop();
    return true;
}


VkService::MusicOwnerList VkService::MusicOwner::parseMusicOwnerList(const QVariant &request_result)
{
    auto list  = request_result.toList();
    MusicOwnerList result;
    foreach (auto item, list) {
        auto map = item.toMap();
        MusicOwner owner;
        owner.songs_count_ = map.value("songs_count").toInt();
        owner.id_ = map.value("id").toInt();
        owner.name_ = map.value("name").toString();
        owner.screen_name_ = map.value("screen_name").toString();
        owner.photo_ = map.value("photo").toUrl();

        result.append(owner);
    }

    return result;
}



/*******************************************************************
 *
 *  VkMusicCache realisation
 *
 ******************************************************************/

VkMusicCache::VkMusicCache(VkService *service, QObject *parent)
    :QObject(parent),
      service_(service),
      current_cashing_index(0),
      is_downloading(false),
      is_aborted(false),
      file_(nullptr),
      network_manager_(new QNetworkAccessManager),
      reply_(nullptr)
{
}

QUrl VkMusicCache::Get(const QUrl &url)
{
    QString cached_filename = CachedFilename(url);
    QUrl result;

    if (InCache(cached_filename)) {
        qLog(Info) << "Use cashed file" << cached_filename;
        result = QUrl("file://" + cached_filename);
    } else {
        result = service_->GetSongPlayUrl(url, false);

        if (service_->isCachingEnabled()) {
            AddToQueue(cached_filename, result);
            current_cashing_index = queue_.size();
        }
    }
    return result;
}


void VkMusicCache::ForceCache(const QUrl &url)
{
    AddToQueue(CachedFilename(url), service_->GetSongPlayUrl(url));
}

void VkMusicCache::BreakCurrentCaching()
{
    if (current_cashing_index > 0) {
        // Current song in queue
        queue_.removeAt(current_cashing_index - 1);
    } else if (current_cashing_index == 0) {
        // Current song is downloading
        if (reply_) {
            reply_->abort();
            is_aborted = true;
        }
    }
}


/***
 * Queue operations
 */

void VkMusicCache::AddToQueue(const QString &filename, const QUrl &download_url)
{
    DownloadItem item;
    item.filename = filename;
    item.url = download_url;
    queue_.push_back(item);
    DownloadNext();
}



/***
 * Downloading
 */

void VkMusicCache::DownloadNext()
{
    if (is_downloading or queue_.isEmpty()) {
        return;
    } else {
        current_download = queue_.first();
        queue_.pop_front();
        current_cashing_index--;

        // Check file path and file existance first
        if (QFile::exists(current_download.filename)) {
            qLog(Warning) << "Tried to overwrite already cached file" << current_download.filename;
            return;
        }

        // Create temporarry file we download to.
        if (file_) {
            qLog(Warning) << "QFile" << file_->fileName() << "is not null";
            delete file_;
        }

        file_ = new QTemporaryFile;
        if (!file_->open(QFile::WriteOnly)) {
            qLog(Warning) << "Can not create temporary file" << file_->fileName()
                          << "Download right away to" << current_download.filename;
        }

        // Start downloading
        is_aborted = false;
        is_downloading = true;
        task_id = service_->app()->task_manager()->
                StartTask(tr("Caching %1")
                          .arg(QFileInfo(current_download.filename).baseName()));
        reply_ = network_manager_->get(QNetworkRequest(current_download.url));
        connect(reply_, SIGNAL(finished()), SLOT(Downloaded()));
        connect(reply_, SIGNAL(readyRead()), SLOT(DownloadReadyToRead()));
        connect(reply_,SIGNAL(downloadProgress(qint64,qint64)), SLOT(DownloadProgress(qint64,qint64)));
        qLog(Info)<< "Start cashing" << current_download.filename  << "from" << current_download.url;
    }
}

void VkMusicCache::DownloadProgress(qint64 bytesReceived, qint64 bytesTotal)
{
    if (bytesTotal) {
        int progress = round(100 * bytesReceived / bytesTotal);
        service_->app()->task_manager()->SetTaskProgress(task_id,progress,100);
    }
}

void VkMusicCache::DownloadReadyToRead()
{
    if (file_) {
        file_->write(reply_->readAll());
    } else {
        qLog(Warning) << "Tried to write recived song to not created file";
    }
}

void VkMusicCache::Downloaded()
{
    service_->app()->task_manager()->SetTaskFinished(task_id);
    if (is_aborted or reply_->error()) {
        if (reply_->error()) {
            qLog(Error) << "Downloading failed" << reply_->errorString();
        }
    } else {
        DownloadReadyToRead(); // Save all recent recived data.

        QString path = service_->cacheDir();

        if (file_->size()  >  0) {
            QDir(path).mkpath(QFileInfo(current_download.filename).path());
            if (file_->copy(current_download.filename)) {
                qLog(Info) << "Cached" << current_download.filename;
            } else {
                qLog(Error) << "Unable to save" << current_download.filename
                              << ":" << file_->errorString();
            }
        } else {
            qLog(Error) << "File" << current_download.filename << "is empty";
        }
    }

    delete file_;
    file_ = nullptr;

    reply_->deleteLater();
    reply_ = nullptr;

    is_downloading = false;
    DownloadNext();
}

/***
 * Utils
 */

inline bool VkMusicCache::InCache(const QString &filename)
{
    return QFile::exists(filename);
}

inline bool VkMusicCache::InCache(const QUrl &url)
{
    return InCache(CachedFilename(url));
}

QString VkMusicCache::CachedFilename(QUrl url)
{
    QStringList args = url.toString().remove("vk://").split('/');

    QString cache_filename;
    if (args.size() == 4) {
        cache_filename = service_->cacheFilename();
        cache_filename.replace("%artist",args[2]);
        cache_filename.replace("%title", args[3]);
    } else {
        qLog(Warning) << "Song url with args" << args << "does not contain artist and title"
                      << "use id as file name for cache.";
        cache_filename = args[1];
    }

    QString cache_dir = service_->cacheDir();
    if (cache_dir.isEmpty()) {
        qLog(Warning) << "Cache dir not defined";
        return "";
    }
    return cache_dir+'/'+cache_filename+".mp3"; //TODO(shed): Maybe use extensiion from link? Seems it's always mp3.
}
