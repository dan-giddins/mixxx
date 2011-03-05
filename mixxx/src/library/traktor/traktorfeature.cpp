// traktorfeature.cpp
// Created 9/26/2010 by Tobias Rafreider

#include <QtDebug>
#include <QMessageBox>
#include <QXmlStreamReader>
#include <QMap>
#include <QSettings>

#include "library/traktor/traktorfeature.h"

#include "library/librarytablemodel.h"
#include "library/missingtablemodel.h"
#include "library/trackcollection.h"
#include "library/treeitem.h"


TraktorFeature::TraktorFeature(QObject* parent, TrackCollection* pTrackCollection):
        m_pTrackCollection(pTrackCollection) {
    m_isActivated = false;
    m_pTraktorTableModel = new TraktorTableModel(this, m_pTrackCollection);
    m_pTraktorPlaylistModel = new TraktorPlaylistModel(this, m_pTrackCollection);
    m_title = tr("Traktor");
    if (!m_database.isOpen()) {
        m_database = QSqlDatabase::addDatabase("QSQLITE", "TRAKTOR_SCANNER");
        m_database.setHostName("localhost");
        m_database.setDatabaseName(MIXXX_DB_PATH);
        m_database.setUserName("mixxx");
        m_database.setPassword("mixxx");

        //Open the database connection in this thread.
        if (!m_database.open()) {
            qDebug() << "Failed to open database for iTunes scanner." << m_database.lastError();
        }
    }
    connect(&m_future_watcher, SIGNAL(finished()), this, SLOT(onTrackCollectionLoaded()));
}

TraktorFeature::~TraktorFeature() {
    if(m_pTraktorTableModel)
        delete m_pTraktorTableModel;
    if(m_pTraktorPlaylistModel)
        delete m_pTraktorPlaylistModel;
}

QVariant TraktorFeature::title() {
    return m_title;
}

QIcon TraktorFeature::getIcon() {
    return QIcon(":/images/library/ic_library_traktor.png");
}
bool TraktorFeature::isSupported() {
    return (QFile::exists(getTraktorMusicDatabase()));

}
TreeItemModel* TraktorFeature::getChildModel() {
    return &m_childModel;
}

void TraktorFeature::refreshLibraryModels()
{

}

void TraktorFeature::activate() {
    qDebug() << "TraktorFeature::activate()";

    if(!m_isActivated){

        m_isActivated =  true;
        /* Ususally the maximum number of threads
         * is > 2 depending on the CPU cores
         * Unfortunately, within VirtualBox
         * the maximum number of allowed threads
         * is 1 at all times We'll need to increase
         * the number to > 1, otherwise importing the music collection
         * takes place when the GUI threads terminates, i.e., on
         * Mixxx shutdown.
         */
        QThreadPool::globalInstance()->setMaxThreadCount(4); //Tobias decided to use 4
        // Let a worker thread do the XML parsing
        m_future = QtConcurrent::run(this, &TraktorFeature::importLibrary, getTraktorMusicDatabase());
        m_future_watcher.setFuture(m_future);
        m_title = tr("Traktor (loading)");
        //calls a slot in the sidebar model such that 'iTunes (isLoading)' is displayed.
        emit (featureIsLoading(this));
    }
    else{
        emit(showTrackModel(m_pTraktorTableModel));
    }


}

void TraktorFeature::activateChild(const QModelIndex& index) {

    if(!index.isValid()) return;

    //access underlying TreeItem object
    TreeItem *item = static_cast<TreeItem*>(index.internalPointer());

    if(item->isPlaylist()){
        qDebug() << "Activate Traktor Playlist: " << item->dataPath().toString();
        m_pTraktorPlaylistModel->setPlaylist(item->dataPath().toString());
        emit(showTrackModel(m_pTraktorPlaylistModel));
    }
}

void TraktorFeature::onRightClick(const QPoint& globalPos) {
}

void TraktorFeature::onRightClickChild(const QPoint& globalPos,
                                            QModelIndex index) {
}

bool TraktorFeature::dropAccept(QUrl url) {
    return false;
}

bool TraktorFeature::dropAcceptChild(const QModelIndex& index, QUrl url) {
    return false;
}

bool TraktorFeature::dragMoveAccept(QUrl url) {
    return false;
}

bool TraktorFeature::dragMoveAcceptChild(const QModelIndex& index,
                                              QUrl url) {
    return false;
}
TreeItem* TraktorFeature::importLibrary(QString file){
    //Give thread a low priority
    QThread* thisThread = QThread::currentThread();
    thisThread->setPriority(QThread::LowestPriority);
    //Invisible root item of Traktor's child model
    TreeItem* root = NULL;
    //Delete all table entries of Traktor feature
    m_database.transaction();
    clearTable("traktor_playlist_tracks");
    clearTable("traktor_library");
    clearTable("traktor_playlists");
    m_database.commit();

    m_database.transaction();
    QSqlQuery query(m_database);
    query.prepare("INSERT INTO traktor_library (artist, title, album, year, genre,comment,"
                   "tracknumber,"
                   "bpm, bitrate,"
                   "duration, location,"
                   "rating,"
                   "key) "
                   "VALUES (:artist, :title, :album, :year, :genre,:comment, :tracknumber,"
                   ":bpm, :bitrate,"
                   ":duration, :location,"
                   ":rating,"
                   ":key)");

    //Parse Trakor XML file using SAX (for performance)
    QFile traktor_file(file);
    if (!traktor_file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "Cannot open Traktor music collection";
        return false;
    }
    QXmlStreamReader xml(&traktor_file);
    bool inCollectionTag = false;
    bool inEntryTag = false;
    bool inPlaylistsTag = false;
    bool isRootFolderParsed = false;
    int nAudioFiles = 0;

    while (!xml.atEnd())
    {
        xml.readNext();
        if(xml.isStartElement())
        {
            if(xml.name() == "COLLECTION")
            {
                inCollectionTag = true;

            }
            /*
             * Each "ENTRY" tag in <COLLECTION> represents a track
             */
            if(inCollectionTag && xml.name() == "ENTRY" )
            {
                inEntryTag = true;
                //parse track
                parseTrack(xml, query);

                ++nAudioFiles; //increment number of files in the music collection
            }
            if(xml.name() == "PLAYLISTS")
            {
                inPlaylistsTag = true;

            }
            if(inPlaylistsTag && !isRootFolderParsed && xml.name() == "NODE"){
                QXmlStreamAttributes attr = xml.attributes();
                QString nodetype = attr.value("TYPE").toString();
                QString name = attr.value("NAME").toString();

                if(nodetype == "FOLDER" && name == "$ROOT"){
                    //process all playlists
                    root = parsePlaylists(xml);
                    isRootFolderParsed = true;
                }
            }

        }
        if(xml.isEndElement())
        {
            if(xml.name() == "COLLECTION")
            {
                inCollectionTag = false;

            }
            if(xml.name() == "ENTRY" && inCollectionTag)
            {
                inEntryTag = false;

            }
            if(xml.name() == "PLAYLISTS" && inPlaylistsTag)
            {
                inPlaylistsTag = false;
            }

        }
    }
    if (xml.hasError()) {
         // do error handling
         qDebug() << "Cannot process Traktor music collection";
         if(root)
             delete root;
         return false;
    }

    qDebug() << "Found: " << nAudioFiles << " audio files in Traktor";
    //initialize TraktorTableModel
    m_database.commit();

    return root;

}
void TraktorFeature::parseTrack(QXmlStreamReader &xml, QSqlQuery &query){
    QString title;
    QString artist;
    QString album;
    QString year;
    QString genre;
    //drive letter
    QString volume;
    QString path;
    QString filename;
    QString location;
    float bpm = 0.0;
    int bitrate = 0;
    QString key;
    //duration of a track
    int playtime = 0;
    int rating = 0;
    QString comment;
    QString tracknumber;

    //get XML attributes of starting ENTRY tag
    QXmlStreamAttributes attr = xml.attributes ();
    title = attr.value("TITLE").toString();
    artist = attr.value("ARTIST").toString();

    //read all sub tags of ENTRY until we reach the closing ENTRY tag
    while(!xml.atEnd())
    {
        xml.readNext();
        if(xml.isStartElement()){
            if(xml.name() == "ALBUM")
            {
                QXmlStreamAttributes attr = xml.attributes ();
                album = attr.value("TITLE").toString();
                tracknumber = attr.value("TRACK").toString();

                continue;
            }
            if(xml.name() == "LOCATION")
            {
                QXmlStreamAttributes attr = xml.attributes ();
                volume = attr.value("VOLUME").toString();
                path = attr.value("DIR").toString();
                filename = attr.value("FILE").toString();
                /*compute the location, i.e, combining all the values
                 * On Windows the volume holds the drive letter e.g., d:
                 * On OS X, the volume is supposed to be "Macintosh HD" at all times,
                 * which is a folder in /Volumes/
                 */
                #if defined(__APPLE__)
                location = "/Volumes/"+volume;
                #else
                location = volume;
                #endif
                location += path.replace(QString(":"), QString(""));
                location += filename;
                continue;
            }
            if(xml.name() == "INFO")
            {
                QXmlStreamAttributes attr = xml.attributes();
                key = attr.value("KEY").toString();
                bitrate = attr.value("BITRATE").toString().toInt() / 1000;
                playtime = attr.value("PLAYTIME").toString().toInt();
                genre = attr.value("GENRE").toString();
                year = attr.value("RELEASE_DATE").toString();
                comment = attr.value("COMMENT").toString();
                QString ranking_str = attr.value("RANKING").toString();
                /* A ranking in Traktor has ranges between 0 and 255 internally.
                 * This is same as the POPULARIMETER tag in IDv2, see http://help.mp3tag.de/main_tags.html
                 *
                 * Our rating values range from 1 to 5. The mapping is defined as follow
                 * ourRatingValue = TraktorRating / 51
                 */
                 if(ranking_str != "" && qVariantCanConvert<int>(ranking_str)){
                    rating = ranking_str.toInt()/51;
                 }
                continue;
            }
            if(xml.name() == "TEMPO")
            {
                QXmlStreamAttributes attr = xml.attributes ();
                bpm = attr.value("BPM").toString().toFloat();
                continue;
            }
        }
        //We leave the infinte loop, if twe have the closing tag "ENTRY"
        if(xml.name() == "ENTRY" && xml.isEndElement()){
            break;
        }
    }

    /* If we reach the end of ENTRY within the COLLECTION tag
     * Save parsed track to database
     */
    query.bindValue(":artist", artist);
    query.bindValue(":title", title);
    query.bindValue(":album", album);
    query.bindValue(":genre", genre);
    query.bindValue(":year", year);
    query.bindValue(":duration", playtime);
    query.bindValue(":location", location);
    query.bindValue(":rating", rating);
    query.bindValue(":comment", comment);
    query.bindValue(":tracknumber", tracknumber);
    query.bindValue(":key", key);
    query.bindValue(":bpm", bpm);
    query.bindValue(":bitrate", bitrate);


    bool success = query.exec();
    if(!success){
        qDebug() << "SQL Error in TraktorTableModel.cpp: line" << __LINE__ << " " << query.lastError();
        return;
    }

}
/*
 * Purpose: Parsing all the folder and playlists of Traktor
 * This is a complex operation since Traktor uses the concept of folders and playlist.
 * A folder can contain folders and playlists. A playlist contains entries but no folders.
 * In other words, Traktor uses a tree structure to organize music. Inner nodes represent folders while
 * leaves are playlists.
 */
TreeItem* TraktorFeature::parsePlaylists(QXmlStreamReader &xml){

    qDebug() << "Process RootFolder";
    //Each playlist is unique and can be identified by a path in the tree structure.
    QString current_path = "";
    QMap<QString,QString> map;

    QString delimiter = "-->";

    TreeItem *rootItem = new TreeItem();
    TreeItem * parent = rootItem;

    bool inPlaylistTag = false;

    QSqlQuery query_insert_to_playlists(m_database);
    query_insert_to_playlists.prepare("INSERT INTO traktor_playlists (name) "
                  "VALUES (:name)");

    QSqlQuery query_insert_to_playlist_tracks(m_database);
    query_insert_to_playlist_tracks.prepare("INSERT INTO traktor_playlist_tracks (playlist_id, track_id) "
                  "VALUES (:playlist_id, :track_id)");

    while(!xml.atEnd())
    {
        //read next XML element
        xml.readNext();

        if(xml.isStartElement())
        {
            if(xml.name() == "NODE"){
                QXmlStreamAttributes attr = xml.attributes();
                QString name = attr.value("NAME").toString();
                QString type = attr.value("TYPE").toString();

               //TODO: What happens if the folder node is a leaf (empty folder)
               // Idea: Hide empty folders :-)
               if(type == "FOLDER")
               {

                    current_path += delimiter;
                    current_path += name;
                    //qDebug() << "Folder: " +current_path << " has parent " << parent->data().toString();
                    map.insert(current_path, "FOLDER");

                    TreeItem * item = new TreeItem(name,current_path, this, parent);
                    parent->appendChild(item);
                    parent = item;
               }
               if(type == "PLAYLIST")
               {
                    current_path += delimiter;
                    current_path += name;
                    //qDebug() << "Playlist: " +current_path << " has parent " << parent->data().toString();
                    map.insert(current_path, "PLAYLIST");

                    TreeItem * item = new TreeItem(name,current_path, this, parent);
                    parent->appendChild(item);
                    // process all the entries within the playlist 'name' having path 'current_path'
                    parsePlaylistEntries(xml,current_path, query_insert_to_playlists, query_insert_to_playlist_tracks);

               }

            }
            if(xml.name() == "ENTRY" && inPlaylistTag){


            }

        }

        if(xml.isEndElement())
        {
            if(xml.name() == "NODE")
            {
                if(map.value(current_path) == "FOLDER"){
                    parent = parent->parent();
                }

                //Whenever we find a closing NODE, remove the last component of the path
                int lastSlash = current_path.lastIndexOf(delimiter);
                int path_length = current_path.size();

                current_path.remove(lastSlash, path_length - lastSlash);



            }
             if(xml.name() == "PLAYLIST")
            {
                inPlaylistTag = false;
            }
            //We leave the infinte loop, if twe have the closing "PLAYLIST" tag
            if(xml.name() == "PLAYLISTS")
            {
                break;
            }
        }

    }
    return rootItem;
}
void TraktorFeature::parsePlaylistEntries(QXmlStreamReader &xml,QString playlist_path, QSqlQuery query_insert_into_playlist, QSqlQuery query_insert_into_playlisttracks)
{
    // In the database, the name of a playlist is specified by the unique path, e.g., /someFolderA/someFolderB/playlistA"
    query_insert_into_playlist.bindValue(":name", playlist_path);
    bool success = query_insert_into_playlist.exec();
    if(!success){
        qDebug() << "SQL Error in TraktorTableModel.cpp: line" << __LINE__ << " " << query_insert_into_playlist.lastError();
        return;
    }
    //Get playlist id
    QSqlQuery id_query(m_database);
    id_query.prepare("select id from traktor_playlists where name=:path");
    id_query.bindValue(":path", playlist_path);
    success = id_query.exec();

    int playlist_id = -1;
    if(success){
        //playlist_id = id_query.lastInsertId().toInt();
        while (id_query.next()) {
            playlist_id = id_query.value(id_query.record().indexOf("id")).toInt();
        }
    }
    else
        qDebug() << "SQL Error in TraktorTableModel.cpp: line" << __LINE__ << " " << id_query.lastError();



    while(!xml.atEnd())
    {
        //read next XML element
        xml.readNext();
        if(xml.isStartElement())
        {
            if(xml.name() == "PRIMARYKEY"){
                QXmlStreamAttributes attr = xml.attributes();
                QString key = attr.value("KEY").toString();
                QString type = attr.value("TYPE").toString();
                if(type == "TRACK")
                {
                    key.replace(QString(":"), QString(""));
                    //TODO: IFDEF
                    #if defined(__WINDOWS__)
                    key.insert(1,":");
                    #else
                    key.prepend("/Volumes/");
                    #endif

                    //insert to database
                    int track_id = -1;
                    QSqlQuery finder_query(m_database);
                    finder_query.prepare("select id from traktor_library where location=:path");
                    finder_query.bindValue(":path", key);
                    success = finder_query.exec();


                    if(success){
                        while (finder_query.next()) {
                            track_id = finder_query.value(finder_query.record().indexOf("id")).toInt();
                        }
                    }
                    else
                        qDebug() << "SQL Error in TraktorTableModel.cpp: line" << __LINE__ << " " << finder_query.lastError();

                    query_insert_into_playlisttracks.bindValue(":playlist_id", playlist_id);
                    query_insert_into_playlisttracks.bindValue(":track_id", track_id);
                    success = query_insert_into_playlisttracks.exec();


                    if(!success){
                        qDebug() << "SQL Error in TraktorFeature.cpp: line" << __LINE__ << " " << query_insert_into_playlisttracks.lastError();
                        qDebug() << "trackid" << track_id << " with path " << key;
                        qDebug() << "playlistname; " << playlist_path <<" with ID " << playlist_id;
                        qDebug() << "-----------------";

                    }

                }

            }
        }
        if(xml.isEndElement()){
            //We leave the infinte loop, if twe have the closing "PLAYLIST" tag
            if(xml.name() == "PLAYLIST")
            {
                break;
            }
        }
    }

}
void TraktorFeature::clearTable(QString table_name)
{
    QSqlQuery query(m_database);
    query.prepare("delete from "+table_name);
    bool success = query.exec();

    if(!success)
        qDebug() << "Could not delete remove old entries from table " << table_name << " : " << query.lastError();
    else
        qDebug() << "Traktor table entries of '" << table_name <<"' have been cleared.";
}
QString TraktorFeature::getTraktorMusicDatabase()
{
    QString musicFolder;
#if defined(__APPLE__)
    musicFolder = QDir::homePath() +"/Documents/Native Instruments/Traktor/collection.nml";
#elif defined(__WINDOWS__)
    QSettings settings("HKEY_CURRENT_USER\\Software\\Native Instruments\\Traktor Pro", QSettings::NativeFormat);
        // if the value method fails it returns QTDir::homePath
    musicFolder = settings.value("RootDirectory", QDir::homePath()).toString();
    musicFolder += "collection.nml";
#elif defined(__LINUX__)
        musicFolder =  QDir::homePath() + "/collection.nml";
#else
        musicFolder = "";
#endif
    qDebug() << "Traktor Library Location=[" << musicFolder << "]";
    return musicFolder;


}
void TraktorFeature::onTrackCollectionLoaded()
{
    TreeItem* root = m_future.result();
    if (root) {
        m_childModel.setRootItem(root);
        m_pTraktorTableModel->select();
        emit(showTrackModel(m_pTraktorTableModel));
        qDebug() << "Traktor library loaded successfully";
    } else {
        QMessageBox::warning(
            NULL,
            tr("Error Loading Traktor Library"),
            tr("There was an error loading your Traktor library. Some of "
               "your Traktor tracks or playlists may not have loaded."));
    }
    //calls a slot in the sidebarmodel such that 'isLoading' is removed from the feature title.
    m_title = tr("Traktor");
    emit(featureLoadingFinished(this));
    activate();
}
