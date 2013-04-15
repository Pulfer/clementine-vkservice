#include "vkurlhandler.h"

#include <QDir>
#include <QFile>
#include <QTemporaryFile>

#include "core/application.h"
#include "core/taskmanager.h"
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
    TRACE VAR(url);

    QStringList args = url.toString().remove("vk://").split("/");

    emit CurrentSongChanged(url);

    if (args.size() < 2) {
        qLog(Error) << "Invalid VK.com URL: " << url.toString()
                    << "Url format should be vk://<source>/<id>."
                    << "For example vk://song/61145020_166946521/Daughtry/Gone Too Soon";
    } else {
        QString action = args[0];
        QString id = args[1];

        if (action == "song") {
            QUrl media_url = songs_cache_->Get(url);
            return LoadResult(url,LoadResult::TrackAvailable,media_url);
        } else {
            qLog(Error) << "Invalid vk.com url action:" << action;
        }
    }
    return LoadResult();
}

void VkUrlHandler::TrackSkipped()
{
    songs_cache_->BreakCurrentCaching();
}

void VkUrlHandler::ForceAddToCache(const QUrl &url)
{
    songs_cache_->ForceCache(url);
}




/******************************
 *  VkMusiccache realisation  *
 */

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
        result = service_->GetSongUrl(url);

        if (service_->isCachingEnabled()) {
            AddToQueue(cached_filename, result);
            current_cashing_index = queue_.size();
        }
    }
    return result;
}


void VkMusicCache::ForceCache(const QUrl &url)
{
    AddToQueue(CachedFilename(url), service_->GetSongUrl(url));
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
        qDebug() << "Caching" << progress << current_download.filename;
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
