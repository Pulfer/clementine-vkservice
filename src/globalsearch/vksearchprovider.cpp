#include "vksearchprovider.h"

#include <algorithm>

#include "core/logging.h"
#include "core/song.h"

VkSearchProvider::VkSearchProvider(Application* app, QObject* parent) :
    SearchProvider(app,parent),
    service_(NULL)
{
}

void VkSearchProvider::Init(VkService *service)
{
    TRACE

    service_ = service;
    SearchProvider::Init("Vk.com", "vk.com",
                         QIcon(":providers/vk.png"),
                         WantsDelayedQueries | CanShowConfig);

    connect(service_, SIGNAL(SongSearchResult(RequestID,SongList)),
            this, SLOT(SongSearchResult(RequestID,SongList)));
    connect(service_, SIGNAL(GroupSearchResult(RequestID,VkService::MusicOwnerList)),
            this, SLOT(GroupSearchResult(RequestID,VkService::MusicOwnerList)));

}

void VkSearchProvider::SearchAsync(int id, const QString &query)
{
    QSettings s;
    s.beginGroup(VkService::kSettingGroup);

    int count = s.value("maxSearchResult",100).toInt();

    RequestID rid(VkService::GlobalSearch);
    songs_recived = false;
    groups_recived = false;
    pending_searches_[rid.id()] = PendingState(id, TokenizeQuery(query));
    service_->SongSearch(rid, query,count,0);
    if (service_->isGroupsInGlobalSearch()){
        service_->GroupSearch(rid,query);
    }
}

bool VkSearchProvider::IsLoggedIn()
{
    return (service_ && service_->HasAccount());
}

void VkSearchProvider::ShowConfig()
{
    service_->ShowConfig();
}

void VkSearchProvider::SongSearchResult(VkService::RequestID rid, SongList songs)
{
    if (rid.type() == VkService::GlobalSearch) {
        ClearSimilarSongs(songs);
        ResultList ret;
        foreach (const Song& song, songs) {
            Result result(this);
            result.metadata_ = song;
            ret << result;
        }
        qLog(Info) << "Found" << songs.count() << "songs.";
        songs_recived = true;
        const PendingState state = pending_searches_[rid.id()];
        emit ResultsAvailable(state.orig_id_, ret);
        MaybeSearchFinished(rid.id());
   }
}

void VkSearchProvider::GroupSearchResult(RequestID rid, const VkService::MusicOwnerList &groups)
{
    if (rid.type() == VkService::GlobalSearch) {
        ResultList ret;
        foreach (const VkService::MusicOwner &group, groups) {
            Result result(this);
            Song song;
            song.set_title(tr("[%0] %1").arg(group.songs_count).arg(group.name));
            song.set_url(QUrl(QString("vk://group/%1").arg(-group.id)));
            song.set_artist(tr(" Group"));
            result.metadata_ = song;
            ret << result;
        }
        qLog(Info) << "Found" << groups.count() << "groups.";
        groups_recived = true;
        const PendingState state = pending_searches_[rid.id()];
        emit ResultsAvailable(state.orig_id_, ret);
        MaybeSearchFinished(rid.id());
   }
}

void VkSearchProvider::MaybeSearchFinished(int id)
{
    if (pending_searches_.keys(PendingState(id, QStringList())).isEmpty() and songs_recived and groups_recived) {
        const PendingState state = pending_searches_.take(id);
        emit SearchFinished(state.orig_id_);
    }
}

void VkSearchProvider::ClearSimilarSongs(SongList &list)
{
    /* Search result sorted by relevance, and better quality songs usualy come first.
     * Stable sort don't mix similar song, so std::unique will remove bad quality copies.
     */

    qStableSort(list.begin(), list.end(), [](const Song &a, const Song &b){
        return (a.artist().localeAwareCompare(b.artist()) > 0)
                or (a.title().localeAwareCompare(b.title()) > 0);
    });

    int old = list.count();

    auto end = std::unique(list.begin(), list.end(), [](const Song &a, const Song &b){
        return (a.artist().localeAwareCompare(b.artist()) == 0)
                and (a.title().localeAwareCompare(b.title()) == 0);
    });

    list.erase(end, list.end());

    qDebug() << "Cleared" << old - list.count() << "items";
}
