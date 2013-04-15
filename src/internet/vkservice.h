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
 * TODO:
 *  Cashing:
 *      - Using playing stream for caching.
 *          First version - return downloading filename to GStreamer.
 *          But GStreamer will not wait untill the file will be downloaded, it's just skip.
 *          Second version  - beforehand load next file, but it's not always possible
 *          to predict correctly, for example if user start to play any song he want.
 *  Groups:
 *      - Realise search.
 *      - Group radio.
 */

#define  VAR(var) qLog(Debug) << ("---    where " #var " =") << (var);
#define  TRACE qLog(Debug) << "--- " << __PRETTY_FUNCTION__ ;

typedef Vreen::OAuthConnection::Scopes Scopes;
typedef uint GroupID;

namespace Vreen {
class Client;
class OAuthConnection;
class Buddy;
}

class SearchBoxWidget;

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

    enum ItemType {        
        Type_Root = InternetModel::TypeCount,

        Type_NeedLogin,

        Type_Loading,
        Type_More,

        Type_Recommendations,
        Type_MyMusic,

        Type_Search
    };

    enum RequestType {
        GlobalSearch,
        LocalSearch,
        MoreLocalSearch,
        UserAudio,
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
    QUrl GetSongUrl(const QUrl &url);

    void SongSearch(RequestID id,const QString &query, int count = 50, int offset = 0);

    void MoreRecommendations();
    Q_SLOT void Search(QString query);
    void MoreSearch();

    /* Settings */
    void UpdateSettings();
    int maxGlobalSearch() { return maxGlobalSearch_; }
    bool isCachingEnabled() { return cachingEnabled_; }
    QString cacheDir() { return cacheDir_; }
    QString cacheFilename() { return cacheFilename_; }
    bool isLoveAddToMyMusic() { return love_is_add_to_mymusic_; }

signals:
    void NameUpdated(QString name);
    void LoginSuccess(bool succ);
    void SongListLoaded(RequestID id, SongList songs);
    void SongSearchResult(RequestID id, const SongList &songs);
    void StopWaiting();
    
public slots:
    void ShowConfig();
    void LoadSongList(uint uid, uint count = 0); // zero means - load full list

private slots:
    /* Connection */
    void ChangeAccessToken(const QByteArray &token, time_t expiresIn);
    void ChangeUid(int uid);
    void OnlineStateChanged(bool online);
    void ChangeMe(Vreen::Buddy*me);
    void Error(Vreen::Client::Error error);

    /* Music */
    void UpdateMyMusic();
    void UpdateRecommendations();
    void FindThisArtist();
    void AddToMyMusic();
    void AddToMyMusicCurrent();
    void RemoveFromMyMusic();
    void AddToCache();
    void CopyShareUrl();

    void SetCurrentSongUrl(const QUrl &url);

    void SongListRecived(RequestID rid, Vreen::AudioItemListReply *reply);
    void CountRecived(RequestID rid, Vreen::IntReply* reply);
    void SongSearchRecived(RequestID id, Vreen::AudioItemListReply *reply);

    void MyMusicLoaded(RequestID rid, const SongList &songs);
    void RecommendationsLoaded(RequestID id, const SongList &songs);
    void SearchResultLoaded(RequestID rid, const SongList &songs);

private:
    /* Interface */
    QStandardItem *CreateAndAppendRow(QStandardItem *parent, VkService::ItemType type);
    void ClearStandartItem(QStandardItem*item);
    void CreateMenu();
    QStandardItem* root_item_;
    QStandardItem* recommendations_;
    QStandardItem* my_music_;
    QStandardItem* search_;
    QVector<QStandardItem*> playlists_;

    QMenu* context_menu_;

    QAction* update_my_music_;
    QAction* update_recommendations_;
    QAction* find_this_artist_;
    QAction* add_to_my_music_;
    QAction* remove_from_my_music_;
    QAction* add_song_to_cache_;
    QAction* copy_share_url_;

    SearchBoxWidget* search_box_;

    /* Connection */
    Vreen::Client *client_;
    Vreen::OAuthConnection *connection_;
    bool hasAccount_;
    int my_id_;
    VkUrlHandler* url_handler_;

    /* Music */
    Vreen::AudioProvider* provider_;
    // Kept when more recent results recived.
    // Using for prevent loading tardy result instead.
    uint last_search_id_;
    QString last_query_;
    Song selected_song_; // Store for context menu actions.
    QUrl current_song_url_; // Store for acctions with now plaing song.
    SongList FromAudioList(const Vreen::AudioItemList &list);
    void AppendSongs(QStandardItem *parent, const SongList &songs);

    /* Settings */
    int maxGlobalSearch_;
    bool cachingEnabled_;
    bool love_is_add_to_mymusic_;
    QString cacheDir_;
    QString cacheFilename_;
};

#endif // VKSERVICE_H