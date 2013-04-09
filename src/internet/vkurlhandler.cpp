#include "vkurlhandler.h"

#include <QDir>

#include "core/logging.h"

#include "vkservice.h"

VkUrlHandler::VkUrlHandler(VkService *service, QObject *parent)
    : UrlHandler(parent),
      service_(service)
{
}

UrlHandler::LoadResult VkUrlHandler::StartLoading(const QUrl &url)
{
    TRACE VAR(url);

    QStringList args = url.toString().remove("vk://").split("/");


    if (args.size() < 2) {
        qLog(Error) << "Invalid VK.com URL: " << url.toString()
                    << "Url format should be vk://<source>/<id>. For example vk://song/2449621_77193878";
    } else {
        QString action = args[0];
        QString id = args[1];

        if (action == "song") {
            QSettings s;
            s.beginGroup(VkService::kSettingGroup);
            bool cashing_enabled = s.value("enable_cashing",false).toBool();
            if (cashing_enabled) {
                qLog(Debug) << CashedFileName(args);
            }
            QUrl media_url = service_->GetSongUrl(id);
            return LoadResult(url,LoadResult::TrackAvailable,media_url);
        } else {
            qLog(Error) << "Invalid vk.com url action:" << action;
        }
    }
    return LoadResult();
}

QString VkUrlHandler::CashedFileName(const QStringList &args)
{
    QSettings s;
    s.beginGroup(VkService::kSettingGroup);
    QString cashe_filename;
    if (args.size() == 4) {
        cashe_filename = s.value("cashe_filename","%artist - %title").toString();
        cashe_filename.replace("%artist",args[2]);
        cashe_filename.replace("%title", args[3]);
    } else {
        qLog(Warning) << "Song url with args" << args << "does not contain artist and title"
                      << "use id as file name for cashe.";
        cashe_filename = args[1];
    }

    QString cashe_path = s.value("cashe_path","").toString();
    if (cashe_path.isEmpty()) {
        qLog(Warning) << "Empty cashe dir.";
        return "";
    }
    return cashe_path+'/'+cashe_filename+".mp3";
}
