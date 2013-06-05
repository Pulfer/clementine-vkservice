#ifndef VKSEARCHDIALOG_H
#define VKSEARCHDIALOG_H

#include <QDialog>
#include <QTimer>

#include "vkservice.h"

namespace Ui {
class VkSearchDialog;
}

typedef VkService::RequestID RequestID;

class VkSearchDialog : public QDialog
{
    Q_OBJECT
    
public:
    explicit VkSearchDialog(VkService *service, QWidget *parent = 0);
    ~VkSearchDialog();

signals:
    void AddBookmark(const VkService::MusicOwner &owner);
    void AddRadioToPlaylist(const VkService::MusicOwner &owner);

private slots:
    void Search(const QString &query);
    void SearchResultLoaded(RequestID id, const VkService::MusicOwnerList &owners);
    void LoadPlaylists();
    void PlaylistsLoaded();
    
private:
    static int kDelayBetwenRequests;

    Ui::VkSearchDialog *ui;
    VkService * service_;
    QString last_query_;
    int last_recieved_id_;
    QTimer timer_;
};

#endif // VKSEARCHDIALOG_H
