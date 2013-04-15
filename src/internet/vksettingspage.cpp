#include "vksettingspage.h"

#include <QDir>
#include <QFileDialog>

#include "ui_vksettingspage.h"
#include "core/application.h"
#include "core/logging.h"
#include "internet/vkservice.h"


VkSettingsPage::VkSettingsPage(SettingsDialog *parent)
    : SettingsPage(parent),
      ui_(new Ui::VkSettingsPage),
      service_(dialog()->app()->internet_model()->Service<VkService>())
{
    ui_->setupUi(this);
    connect(service_, SIGNAL(LoginSuccess(bool)),
            SLOT(LoginSuccess(bool)));
    connect(ui_->choose_path, SIGNAL(clicked()),
            SLOT(CasheDirBrowse()));
    connect(ui_->reset, SIGNAL(clicked()),
            SLOT(ResetCasheFilenames()));
}

VkSettingsPage::~VkSettingsPage()
{
    delete ui_;
}

void VkSettingsPage::Load()
{
    service_->UpdateSettings();

    ui_->maxGlobalSearch->setValue(service_->maxGlobalSearch());
    ui_->enable_caching->setChecked(service_->isCachingEnabled());
    ui_->cache_dir->setText(service_->cacheDir());
    ui_->cache_filename->setText(service_->cacheFilename());
    ui_->love_button_is_add_to_mymusic->setChecked(service_->isLoveAddToMyMusic());

    if (service_->HasAccount()) {
        Login();
    } else {
        Logout();
    }
}

void VkSettingsPage::Save()
{
    QSettings s;
    s.beginGroup(VkService::kSettingGroup);

    s.setValue("max_global_search",ui_->maxGlobalSearch->value());
    s.setValue("cache_enabled",ui_->enable_caching->isChecked());
    s.setValue("cache_dir",ui_->cache_dir->text());
    s.setValue("cache_filename", ui_->cache_filename->text());
    s.setValue("love_is_add_to_my_music", ui_->love_button_is_add_to_mymusic->isChecked());

    service_->UpdateSettings();
}

void VkSettingsPage::Login()
{
    qLog(Debug) << "Login clicked";
    ui_->account->setEnabled(false);
    service_->Login();
}

void VkSettingsPage::LoginSuccess(bool succ)
{
    qLog(Debug) << "LoginSuccess" << succ;
    if (succ) {
        ui_->login_button->setText("Logout");

        ui_->name->setText("Loading...");
        connect(service_, SIGNAL(NameUpdated(QString)),
                ui_->name, SLOT(setText(QString)));

        connect(ui_->login_button, SIGNAL(clicked()),
                SLOT(Logout()));
        disconnect(ui_->login_button, SIGNAL(clicked()),
                   this, SLOT(Login()));
    }
    ui_->account->setEnabled(true);
}

void VkSettingsPage::Logout()
{
    qLog(Debug) << "Logout clicked";
    service_->Logout();

    ui_->login_button->setText("Login");
    ui_->name->setText("");

    connect(ui_->login_button, SIGNAL(clicked()),
            SLOT(Login()));
    disconnect(ui_->login_button, SIGNAL(clicked()),
               this, SLOT(Logout()));
    ui_->account->setEnabled(true);
}

void VkSettingsPage::CasheDirBrowse()
{
    QString directory = QFileDialog::getExistingDirectory(
          this, tr("Choose Vk.com cashe directory"), ui_->cache_dir->text());
    if (directory.isEmpty())
      return;

    ui_->cache_dir->setText(QDir::toNativeSeparators(directory));
}

void VkSettingsPage::ResetCasheFilenames()
{
    ui_->cache_filename->setText(VkService::kDefCacheFilename);
}

