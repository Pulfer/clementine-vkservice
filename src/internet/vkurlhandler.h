#ifndef VKURLHANDLER_H
#define VKURLHANDLER_H

#include "core/urlhandler.h"
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class VkService;
class VkMusicCache;

class VkUrlHandler : public UrlHandler
{
    Q_OBJECT
public:
    VkUrlHandler(VkService* service, QObject *parent);
    QString scheme() const { return "vk"; }
    QIcon icon() const { return QIcon(":providers/vk.png"); }
    LoadResult StartLoading(const QUrl &url);
    void TrackSkipped();
    void ForceAddToCache(const QUrl &url);
    LoadResult LoadNext(const QUrl &url);

private:
    VkService* service_;
    VkMusicCache* songs_cache_;
};






#endif // VKURLHANDLER_H
