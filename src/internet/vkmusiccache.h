#ifndef VKMUSICCACHE_H
#define VKMUSICCACHE_H

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QTemporaryFile>
#include <QUrl>

class VkService;

class VkMusicCache : public QObject
{
    Q_OBJECT
public:
    explicit VkMusicCache(VkService* service, QObject *parent = 0);
    ~VkMusicCache() {}
    // Return file path if file in cache otherwise
    // return internet url and add song to caching queue
    QUrl Get(const QUrl &url);
    void ForceCache(const QUrl &url);
    void BreakCurrentCaching();
    bool InCache(const QUrl &url);

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
    // Is zero if song downloading now, and less that zero if current song not caching or cached.
    int current_cashing_index;
    DownloadItem current_download;
    bool is_downloading;
    bool is_aborted;
    int task_id;
    QFile *file_;
    QNetworkAccessManager *network_manager_;
    QNetworkReply *reply_;
};

#endif // VKMUSICCACHE_H
