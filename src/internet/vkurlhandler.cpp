#include "vkurlhandler.h"

#include "core/application.h"
#include "core/logging.h"

#include "vkservice.h"

VkUrlHandler::VkUrlHandler(VkService *service, QObject *parent)
    : UrlHandler(parent),
      service_(service),
      songs_cache_(new VkMusicCache(service))
{
}

UrlHandler::LoadResult VkUrlHandler::StartLoading(const QUrl &url)
{
    QStringList args = url.toString().remove("vk://").split("/");

    if (args.size() < 2) {
        qLog(Error) << "Invalid VK.com URL: " << url.toString()
                    << "Url format should be vk://<source>/<id>."
                    << "For example vk://song/61145020_166946521/Daughtry/Gone Too Soon";
    } else {
        QString action = args[0];
        QString id = args[1];

        if (action == "song") {
            service_->SetCurrentSongFromUrl(url);
            return LoadResult(url, LoadResult::TrackAvailable, songs_cache_->Get(url));
        } else if (action == "group"){
            return service_->GetGroupNextSongUrl(url);
        } else {
            qLog(Error) << "Invalid vk.com url action:" << action;
        }
    }
    return LoadResult();
}

void VkUrlHandler::TrackSkipped()
{
    service_->cache()->BreakCurrentCaching();
}

void VkUrlHandler::ForceAddToCache(const QUrl &url)
{
    service_->cache()->ForceCache(url);
}

UrlHandler::LoadResult VkUrlHandler::LoadNext(const QUrl &url)
{
    if (url.toString().startsWith("vk://group"))
        return StartLoading(url);
    else
        return LoadResult();
}
