#ifndef VKURLHANDLER_H
#define VKURLHANDLER_H

#include "core/urlhandler.h"
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>

class VkService;
class VkMusicCashe;

class VkUrlHandler : public UrlHandler
{
public:
    VkUrlHandler(VkService* service, QObject *parent);
    QString scheme() const { return "vk"; }
    QIcon icon() const { return QIcon(":providers/vk.png"); }
    LoadResult StartLoading(const QUrl &url);
    void TrackSkipped();
    void ForceAddToCashe(const QUrl &url);

private:
    VkService* service_;
    VkMusicCashe* cashe_;
    QString CashedFileName(const QStringList &args);
};

class VkMusicCashe : public QObject
{
    Q_OBJECT
public:
    explicit VkMusicCashe(QObject *parent = 0);
    ~VkMusicCashe() {}
    void Add(const QString &cashed_filename, const QUrl &download_url);
    void BreakLastCashing();
    bool IsContain(const QString &cashed_filename);

private slots:
    void Do();
    void DownloadProgress(qint64 bytesReceived, qint64 bytesTotal);
    void DownloadReadyToRead();
    void Downloaded();

private:
    struct DownloadItem {
        QString filename;
        QUrl url;
    };

    QList<DownloadItem> queue_;
    DownloadItem current_;
    bool is_downloading;
    bool is_aborted;

    QFile *file_;

    QNetworkAccessManager *network_manager_;
    QNetworkReply *reply_;
};

#endif // VKURLHANDLER_H
