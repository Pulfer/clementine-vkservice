#include "vkurlhandler.h"

#include <QDir>
#include <QFile>
#include <QTemporaryFile>

#include "core/logging.h"

#include "vkservice.h"

VkUrlHandler::VkUrlHandler(VkService *service, QObject *parent)
    : UrlHandler(parent),
      service_(service),
      cashe_(new VkMusicCashe)
{
}

UrlHandler::LoadResult VkUrlHandler::StartLoading(const QUrl &url)
{
    TRACE VAR(url);

    QStringList args = url.toString().remove("vk://").split("/");


    if (args.size() < 2) {
        qLog(Error) << "Invalid VK.com URL: " << url.toString()
                    << "Url format should be vk://<source>/<id>."
                    << "For example vk://song/61145020_166946521/Daughtry/Gone Too Soon";
    } else {
        QString action = args[0];
        QString id = args[1];

        if (action == "song") {
            QSettings s;
            s.beginGroup(VkService::kSettingGroup);       
            bool cashing_enabled = s.value("enable_cashing",false).toBool();

            QString cashed_filename = CashedFileName(args);
            if (cashe_->IsContain(cashed_filename)) {
                qLog(Info) << "Using cashed file" << cashed_filename;
                return LoadResult(url,LoadResult::TrackAvailable,
                                  QUrl("file://"+cashed_filename));
            }

            QUrl media_url = service_->GetSongUrl(id);

            if (cashing_enabled) {
                cashe_->Add(cashed_filename,media_url);
            }

            return LoadResult(url,LoadResult::TrackAvailable,media_url);
        } else {
            qLog(Error) << "Invalid vk.com url action:" << action;
        }
    }
    return LoadResult();
}

void VkUrlHandler::TrackSkipped()
{
    QSettings s;
    s.beginGroup(VkService::kSettingGroup);
    bool cashing_enabled = s.value("enable_cashing",false).toBool();

    if (cashing_enabled) {
        cashe_->BreakLastCashing();
    }
}

void VkUrlHandler::ForceAddToCashe(const QUrl &url)
{
    qLog(Info) << "Force add to cashe" << url;
    QStringList args = url.toString().remove("vk://").split("/");

    QString cashed_filename = CashedFileName(args);
    if (QFile::exists(cashed_filename)) {
        QFile::remove(cashed_filename);
    }

    QUrl media_url = service_->GetSongUrl(args[1]);

    cashe_->Add(cashed_filename, media_url);
}

QString VkUrlHandler::CashedFileName(const QStringList &args)
{
    QSettings s;
    s.beginGroup(VkService::kSettingGroup);
    QString cashe_filename;
    if (args.size() == 4) {
        cashe_filename = s.value("cashe_filename",VkService::kDefCasheFilename).toString();
        cashe_filename.replace("%artist",args[2]);
        cashe_filename.replace("%title", args[3]);
    } else {
        qLog(Warning) << "Song url with args" << args << "does not contain artist and title"
                      << "use id as file name for cashe.";
        cashe_filename = args[1];
    }

    QString cashe_path = s.value("cashe_path",VkService::kDefCachePath()).toString();
    if (cashe_path.isEmpty()) {
        qLog(Warning) << "Cashe dir not defined";
        return "";
    }
    return cashe_path+'/'+cashe_filename+".mp3"; //TODO(shed): Maybe use extensiion from link? Seems it's always mp3.
}



/***
 *  VkMusicCashe realisation
 */

VkMusicCashe::VkMusicCashe(QObject *parent)
    :QObject(parent),
      is_downloading(false),
      is_aborted(false),
      file_(nullptr),
      network_manager_(new QNetworkAccessManager),
      reply_(nullptr)
{

}

void VkMusicCashe::Do()
{
    if (is_downloading or queue_.isEmpty()) {
        return;
    } else {
        is_downloading = true;
        current_ = queue_.first();
        queue_.pop_front();

        // Check file path first
        QSettings s;
        s.beginGroup(VkService::kSettingGroup);
        QString path = s.value("cache_path",VkService::kDefCachePath()).toString();

        // Check file existance and availability
        if (QFile::exists(current_.filename)) {
            qLog(Warning) << "Tried to overwrite already cashed file.";
            return;
        }

        if (file_) {
            qLog(Warning) << "QFile" << file_->fileName() << "is not null";
            delete file_;
        }

        file_ = new QTemporaryFile;
        if (!file_->open(QFile::WriteOnly)) {
            qLog(Error) << "Can not create temporary file" << file_->fileName()
                        << "Download right away to" << current_.filename;
        }

        // Start downloading
        is_aborted = false;
        reply_ = network_manager_->get(QNetworkRequest(current_.url));
        connect(reply_, SIGNAL(finished()), SLOT(Downloaded()));
        connect(reply_, SIGNAL(readyRead()), SLOT(DownloadReadyToRead()));
        connect(reply_,SIGNAL(downloadProgress(qint64,qint64)), SLOT(DownloadProgress(qint64,qint64)));

        qLog(Info)<< "Start cashing" << current_.filename  << "from" << current_.url;
    }
}

void VkMusicCashe::DownloadProgress(qint64 bytesReceived, qint64 bytesTotal)
{
//    qLog(Info) << "Cashing" << bytesReceived << "\\" << bytesTotal << "of" << current_.url
//               << "to" << current_.filename;
}

void VkMusicCashe::DownloadReadyToRead()
{
    if (file_) {
        file_->write(reply_->readAll());
    } else {
        qLog(Warning) << "Tried to write recived song to not created file";
    }
}

void VkMusicCashe::Downloaded()
{
    if (is_aborted or reply_->error()) {
        if (reply_->error()) {
            qLog(Error) << "Downloading failed" << reply_->errorString();
        }
    } else {
        DownloadReadyToRead(); // Save all recent recived data.

        QSettings s;
        s.beginGroup(VkService::kSettingGroup);
        QString path =s.value("cashe_path",VkService::kDefCachePath()).toString();

        QDir(path).mkpath(QFileInfo(current_.filename).path());
        if (file_->copy(current_.filename)) {
            qLog(Info) << "Cashed" << current_.filename;
        } else {
            qLog(Warning) << "Unable to save" << current_.filename
                          << ":" << file_->errorString();
        }
    }

    delete file_;
    file_ = nullptr;

    reply_->deleteLater();
    reply_ = nullptr;

    is_downloading = false;
    Do();
}


void VkMusicCashe::Add(const QString &cashed_filename, const QUrl &download_url)
{
    DownloadItem item;
    item.filename = cashed_filename;
    item.url = download_url;
    queue_.push_back(item);
    Do();
}

void VkMusicCashe::BreakLastCashing()
{
    if (queue_.length() > 0){
        queue_.pop_back();
    } else {
        is_aborted = true;
        if (reply_) {
            reply_->abort();
        }
    }
}

bool VkMusicCashe::IsContain(const QString &cashed_filename)
{
    return QFile::exists(cashed_filename);
}
