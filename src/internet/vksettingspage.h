#ifndef VKSETTINGSPAGE_H
#define VKSETTINGSPAGE_H

#include "ui/settingspage.h"

#include <QModelIndex>
#include <QWidget>

class VkService;
class Ui_VkSettingsPage;

class VkSettingsPage : public SettingsPage
{
    Q_OBJECT

public:
    VkSettingsPage(SettingsDialog* parent);
    ~VkSettingsPage();

    void Load();
    void Save();

private slots:
    void LoginSuccess(bool succ);

    void Login();
    void Logout();

    void CasheDirBrowse();
    void ResetCasheFilenames();

private:
    Ui_VkSettingsPage* ui_;
    VkService* service_;
};

#endif // VKSETTINGSPAGE_H
