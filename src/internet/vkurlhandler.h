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
public:
    VkUrlHandler(VkService* service, QObject *parent);
    QString scheme() const { return "vk"; }
    QIcon icon() const { return QIcon(":providers/vk.png"); }
    LoadResult StartLoading(const QUrl &url);
    void TrackSkipped();
    void ForceAddToCache(const QUrl &url);

private:
    VkService* service_;
    VkMusicCache* songs_cache_;
};

class VkMusicCache : public QObject
{
    Q_OBJECT
public:
    explicit VkMusicCache(VkService* service, QObject *parent = 0);
    ~VkMusicCache() {}
    QUrl Get(const QUrl &url);
    void ForceCache(const QUrl &url);
    void BreakCurrentCaching();

private slots:
    bool InCache(const QString &filename);

    void AddToQueue(const QString &filename, const QUrl &download_url);

    void DownloadNext();
    void DownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void DownloadReadyToRead();
    void Downloaded();

private:
    struct DownloadItem {
        QString filename;
        QUrl url;

        bool operator ==(const DownloadItem &rhv) {
            return filename == rhv.filename;
        }
    };

    QString CachedFilename(QUrl url);

    VkService* service_;

    QList<DownloadItem> queue_;
    // Contain index of current song in queue, need for removing if song was skipped.
    // Is zero if song downloading now, and less that zero if current song not caching.
    int current_cashing_index;

    DownloadItem current_download;
    bool is_downloading;
    bool is_aborted;
    int task_id;


    QFile *file_;

    QNetworkAccessManager *network_manager_;
    QNetworkReply *reply_;
};

#endif // VKURLHANDLER_H
