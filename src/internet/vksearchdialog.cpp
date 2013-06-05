#include "vksearchdialog.h"
#include "vkservice.h"
#include "ui_vksearchdialog.h"

#include <QTimer>

int VkSearchDialog::kDelayBetwenRequests = 500; // ms

VkSearchDialog::VkSearchDialog(VkService *service, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::VkSearchDialog),
    service_(service),
    last_recieved_id_(-1)
{
    ui->setupUi(this);
    connect(ui->queryLine, SIGNAL(textChanged(QString)),
            SLOT(Search(QString)));
    connect(service_, SIGNAL(BookmarkSearchResult(RequestID,VkService::MusicOwnerList)),
            SLOT(SearchResultLoaded(RequestID, VkService::MusicOwnerList)));
}

VkSearchDialog::~VkSearchDialog()
{
    delete ui;
}


void VkSearchDialog::Search(const QString &query)
{
    service_->BookmarkSearch(RequestID(VkService::Bookmarks),query);
}

void VkSearchDialog::SearchResultLoaded(RequestID id, const VkService::MusicOwnerList &owners)
{
    ui->albumCombo->clear();
    foreach(const VkService::MusicOwner owner, owners){
        ui->albumCombo->addItem(owner.name());
    }
}

void VkSearchDialog::LoadPlaylists()
{
}

void VkSearchDialog::PlaylistsLoaded()
{
}
