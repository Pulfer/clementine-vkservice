#include "vkurlhandler.h"

#include <QDir>
#include <QFile>

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
        qLog(Warning) << "Empty cashe dir.";
        return "";
    }
    return cashe_path+'/'+cashe_filename+".mp3";
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

        QDir(path).mkpath(QFileInfo(current_.filename).path());

        // Check file existance and availability
        if (QFile::exists(current_.filename)) {
            qLog(Warning) << "Tried to overwrite already cashed file.";
            return;
        }

        if (file_) {
            qLog(Warning) << "QFile" << file_->fileName() << "is not null";
            delete file_;
        }
        file_ = new QFile(current_.filename);

        if(!file_->open(QIODevice::WriteOnly))
        {
            qLog(Error) << "Unable to save the file" << current_.filename
                        << ":" << file_->errorString();
            delete file_;
            file_ = NULL;
            return;
        }

        // Start downloading
        is_aborted = false;
        reply_ = network_manager_->get(QNetworkRequest(current_.url));
        connect(reply_, SIGNAL(finished()), SLOT(Downloaded()));
        connect(reply_, SIGNAL(readyRead()), SLOT(DownloadReadyToRead()));
        connect(reply_,SIGNAL(downloadProgress(qint64,qint64)), SLOT(DownloadProgress(qint64,qint64)));

        qLog(Info)<< "Next download" << current_.filename  << "from" << current_.url;
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
    if (is_aborted) {
        if (file_) {
            file_->close();
            file_->remove();

            QSettings s;
            s.beginGroup(VkService::kSettingGroup);
            QString path = s.value("cache_path",VkService::kDefCachePath()).toString();

            QDir(path).rmpath(QFileInfo(current_.filename).path());
        }
    } else {
        DownloadReadyToRead();
        file_->flush();
        file_->close();
        if (reply_->error()) {
            qLog(Error) << "Downloading failed" << reply_->errorString();
        }
    }

    delete file_;
    file_ = NULL;
    reply_->deleteLater();
    reply_ = NULL;

    qLog(Info) << "Cashed" << current_.filename;
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
    return QFile::exists(cashed_filename);;
}
