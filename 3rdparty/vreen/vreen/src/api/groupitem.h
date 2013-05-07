/****************************************************************************
**
** Vreen - vk.com API Qt bindings
**
** Copyright © 2012 Aleksey Sidorov <gorthauer87@ya.ru>
**
*****************************************************************************
**
** $VREEN_BEGIN_LICENSE$
** This program is free software: you can redistribute it and/or modify
** it under the terms of the GNU Lesser General Public License as published by
** the Free Software Foundation, either version 3 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
** See the GNU Lesser General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program.  If not, see http://www.gnu.org/licenses/.
** $VREEN_END_LICENSE$
**
****************************************************************************/

#ifndef GROUPITEM_H
#define GROUPITEM_H

#include <QSharedDataPointer>
#include <QVariant>

#include "vk_global.h"
#include "contact.h"

class QUrl;
namespace Vreen {

class Client;
class GroupItemData;

class VK_SHARED_EXPORT GroupItem
{
public:
    GroupItem();
    GroupItem(const GroupItem &);
    GroupItem &operator=(const GroupItem &);
    ~GroupItem();

    enum Type {
        Type_Unknown,
        Type_Group,
        Type_Event,
        Type_Page
    };

    int gid() const;
    void setGid(int gid);

    QString name() const;
    void setName(const QString &name);

    QString screenName() const;
    void setScreenName(const QString &scr_name);

    bool isClosed() const;
    void setClosed(bool closed);

    bool isAdmin() const;
    void setAdmin(bool is_admin);

    bool isMember() const;
    void setMember(bool is_member);

    Type type() const;
    void setType(Type t);

    QUrl photo(Contact::PhotoSize size = Contact::PhotoSizeSmall) const;
    void setPhoto(const QUrl &photo,
                  Contact::PhotoSize size = Contact::PhotoSizeSmall);
private:
    QSharedDataPointer<GroupItemData> data;
};
typedef QList<GroupItem> GroupItemList;

} // namespace Vreen


Q_DECLARE_METATYPE(Vreen::GroupItem)
Q_DECLARE_METATYPE(Vreen::GroupItemList)

#endif // GROUPITEM_H
