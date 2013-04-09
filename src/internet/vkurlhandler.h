#ifndef VKURLHANDLER_H
#define VKURLHANDLER_H

#include "core/urlhandler.h"

class VkService;

class VkUrlHandler : public UrlHandler
{
public:
    VkUrlHandler(VkService* service, QObject *parent);
    QString scheme() const { return "vk"; }
    QIcon icon() const { return QIcon(":providers/vk.png"); }
    LoadResult StartLoading(const QUrl &url);

private:
    VkService* service_;
    QString CashedFileName(const QStringList &args);
};

#endif // VKURLHANDLER_H
