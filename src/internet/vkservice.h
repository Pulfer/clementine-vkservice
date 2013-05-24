#ifndef VKSERVICE_H
#define VKSERVICE_H

#include "internetservice.h"
#include "internetmodel.h"
#include "core/song.h"

#include "vreen/auth/oauthconnection.h"
#include "vreen/audio.h"
#include "vreen/contact.h"

#include "vkurlhandler.h"

/***
 * TODO(Vk): SUMMARY
 *  Cashing:
 *      - Using playing stream for caching.
 *          First version - return downloading filename to GStreamer.
 *          But GStreamer will not wait untill the file will be downloaded, it's just skip.
 *          Second version  - beforehand load next file, but it's not always possible
 *          to predict correctly, for example if user start to play any other song he want.
 *  Groups:
 *      - Maybe store bookmarks in vk servers, for sync between platforms/computers for same user.
 *      - Maybe skip group radio if user press next before next song is received or any time out
 *      - Use dynamic playlist instead/with radio.
 *
 *  Ui:
 *      - Actions should work with multiple selected items in playlist,
 *          for example if user want to add many songs to his library.
 */

#define  VAR(var) qLog(Debug) << ("---    where " #var " =") << (var);
#define  TRACE qLog(Debug) << "--- " << __PRETTY_FUNCTION__ ;

typedef Vreen::OAuthConnection::Scopes Scopes;

namespace Vreen {
class Client;
class OAuthConnection;
class Buddy;
}

class SearchBoxWidget;
class VkMusicCache;

class VkService : public InternetService
{
    Q_OBJECT
public:
    explicit VkService(Application* app, InternetModel* parent);
    ~VkService();

    static const char* kServiceName;
    static const char* kSettingGroup;
    static const char* kUrlScheme;
    static const uint  kApiKey;
    static const Scopes kScopes;
    static const char* kDefCacheFilename;
    static QString kDefCacheDir();
    static const int kMaxVkSongList;
    static const int kCustomSongCount;

    enum ItemType {        
        Type_Root = InternetModel::TypeCount,

        Type_NeedLogin,

        Type_Loading,
        Type_More,

        Type_Recommendations,
        Type_MyMusic,
        Type_Bookmark,

        Type_Search
    };

    enum Role { Role_MusicOwnerMetadata = InternetModel::RoleCount };

    enum RequestType {
        GlobalSearch,
        LocalSearch,
        MoreLocalSearch,
        UserAudio,
        MoreUserAudio,
        UserRecomendations
    };

    // The simple structure allows the handler to determine
    // how to react to the received request or quickly skip unwanted.
    struct RequestID {
        RequestID(RequestType type, int id = 0)
            : type_(type)
        {
            switch (type) {
            case UserAudio:
            case UserRecomendations:
                id_ = id; // For User/Group actions id is uid or gid...
                break;
            default:
                id_= last_id_++; // otherwise is increasing unique number.
                break;
            }
        }
        int id() const { return id_; }
        RequestType type() const { return type_; }
    private:
        static uint last_id_;
        int id_;
        RequestType type_;
    };

    // Store information about user or group
    // using in bookmarks.
    class MusicOwner
    {
    public:
        MusicOwner() :
            songs_count_(0),
            id_(0)
        {}

        explicit MusicOwner(const QUrl &group_url);
        Song toOwnerRadio() const;
        QString name() const { return name_; }
        int id() const { return id_; }
        static QList<MusicOwner> parseMusicOwnerList(const QVariant &request_result);
    private:
        friend QDataStream &operator <<(QDataStream &stream, const VkService::MusicOwner &val);
        friend QDataStream &operator >>(QDataStream &stream, VkService::MusicOwner &val);
        friend QDebug operator<< (QDebug d, const MusicOwner &owner);

        int songs_count_;
        int id_; // if id > 0 is user otherwise id group
        QString name_;
        QString screen_name_; //name used in url http://vk.com/<screen_name> for example: http://vk.com/shedward
        QUrl photo_;
    };

    typedef QList<MusicOwner> MusicOwnerList;

    /* InternetService interface */
    QStandardItem* CreateRootItem();
    void LazyPopulate(QStandardItem *parent);
    void ShowContextMenu(const QPoint &global_pos);
    void ItemDoubleClicked(QStandardItem *item);
    QList<QAction*> playlistitem_actions(const Song &song);
    Application* app() { return app_; }

    /* Interface*/
    void RefreshRootSubitems();
    QWidget* HeaderWidget() const;

    /* Connection */
    void Login();
    void Logout();
    bool HasAccount() const { return hasAccount_; }
    bool WaitForReply(Vreen::Reply *reply);

    /* Music */
    VkMusicCache* cache() { return cache_; }
    void SetCurrentSongFromUrl(const QUrl &url); // Used if song taked from cache.
    QUrl GetSongPlayUrl(const QUrl &url, bool is_playing = true);
    UrlHandler::LoadResult GetGroupNextSongUrl(const QUrl& url); // Return random song result from group playlist.

    void SongSearch(RequestID id,const QString &query, int count = 50, int offset = 0);
    void GroupSearch(RequestID id, const QString &query, int count = 20, int offset = 0);

    /* Settings */
    void UpdateSettings();
    int maxGlobalSearch() { return maxGlobalSearch_; }
    bool isCachingEnabled() { return cachingEnabled_; }
    bool isGroupsInGlobalSearch() { return groups_in_global_search_; }
    QString cacheDir() { return cacheDir_; }
    QString cacheFilename() { return cacheFilename_; }
    bool isLoveAddToMyMusic() { return love_is_add_to_mymusic_; }

signals:
    void NameUpdated(QString name);
    void LoginSuccess(bool succ);
    void SongListLoaded(RequestID id, SongList songs);
    void SongSearchResult(RequestID id, const SongList &songs);
    void GroupSearchResult(RequestID id, const VkService::MusicOwnerList &groups);
    void StopWaiting();
    
public slots:
    void ShowConfig();
    void LoadSongList(int uid, uint count = 0); // zero means - load full list

private slots:
    /* Connection */
    void ChangeAccessToken(const QByteArray &token, time_t expiresIn);
    void ChangeUid(int uid);
    void OnlineStateChanged(bool online);
    void ChangeMe(Vreen::Buddy*me);
    void Error(Vreen::Client::Error error);

    /* Music */
    void UpdateMyMusic();
    void UpdateBookmarkSongs();
    void LoadBookmarkSongs(QStandardItem *item);
    void SearchSongs(QString query);
    void MoreSearch();
    void UpdateRecommendations();
    void MoreRecommendations();
    void FindThisArtist();
    void AddToMyMusic();
    void AddToMyMusicCurrent();
    void RemoveFromMyMusic();
    void AddToCache();
    void CopyShareUrl();

    void AddSelectedToBookmarks();
    void RemoveFromBookmark();

    void SongListRecived(RequestID rid, Vreen::AudioItemListReply *reply);
    void CountRecived(RequestID rid, Vreen::IntReply* reply);
    void SongSearchRecived(RequestID id, Vreen::AudioItemListReply *reply);
    void GroupSearchRecived(RequestID id, Vreen::Reply *reply);

    void MyMusicLoaded(RequestID rid, const SongList &songs);
    void BookmarkSongsLoaded(RequestID rid, const SongList &songs);
    void RecommendationsLoaded(RequestID id, const SongList &songs);
    void SearchResultLoaded(RequestID rid, const SongList &songs);

private:
    /* Interface */
    QStandardItem *CreateAndAppendRow(QStandardItem *parent, VkService::ItemType type);
    void ClearStandartItem(QStandardItem*item);
    QStandardItem * GetBookmarkItemById(int id);
    void CreateMenu();
    QStandardItem* root_item_;
    QStandardItem* recommendations_;
    QStandardItem* my_music_;
    QStandardItem* search_;

    QMenu* context_menu_;

    QAction* update_my_music_;
    QAction* update_recommendations_;
    QAction* update_bookmark_;
    QAction* find_this_artist_;
    QAction* add_to_my_music_;
    QAction* remove_from_my_music_;
    QAction* add_song_to_cache_;
    QAction* copy_share_url_;
    QAction* add_to_bookmarks_;
    QAction* remove_from_bookmarks_;

    SearchBoxWidget* search_box_;

    /* Connection */
    Vreen::Client *client_;
    Vreen::OAuthConnection *connection_;
    bool hasAccount_;
    int my_id_;
    VkUrlHandler* url_handler_;

    /* Music */
    Song FromAudioItem(const Vreen::AudioItem &item);
    SongList FromAudioList(const Vreen::AudioItemList &list);
    void AppendSongs(QStandardItem *parent, const SongList &songs);

    QStandardItem *AppendBookmark(const MusicOwner &owner);
    void SaveBookmarks();
    void LoadBookmarks();

    Vreen::AudioProvider* audio_provider_;
    VkMusicCache* cache_;
    // Keeping when more recent results recived.
    // Using for prevent loading tardy result instead.
    uint last_search_id_;
    QString last_query_;
    Song selected_song_; // Store for context menu actions.
    Song current_song_; // Store for actions with now playing song.
    QUrl current_group_url_;

    /* Settings */
    int maxGlobalSearch_;
    bool cachingEnabled_;
    bool love_is_add_to_mymusic_;
    bool groups_in_global_search_;
    QString cacheDir_;
    QString cacheFilename_;
};

Q_DECLARE_METATYPE(VkService::MusicOwner)

QDataStream& operator<<(QDataStream & stream, const VkService::MusicOwner & pen);
QDataStream& operator>>(QDataStream & stream, VkService::MusicOwner & pen);
QDebug operator<< (QDebug d, const VkService::MusicOwner &owner);

#endif // VKSERVICE_H
