/****************************************************************************
 * Copyright (C) 2016,2017 Maschell
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 ****************************************************************************/
#ifndef _ConfigReader_H_
#define _ConfigReader_H_

#include <string>
#include <vector>

#include "ControllerPatcherIncludes.hpp"

class ConfigReader{
    friend class ControllerPatcher;
    friend class ConfigParser;
    private:
        static ConfigReader *getInstance() {
            if(!instance){
                instance = new ConfigReader();
            }
            return instance;
        }

        static void destroyInstance() {
            if(instance){
                delete instance;
                instance = NULL;
            }
        }

        static s32 getNumberOfLoadedFiles(){
            return ConfigReader::numberValidFiles;
        }

        static void increaseNumberOfLoadedFiles(){
            ConfigReader::numberValidFiles++;
        }
        bool ReadConfigs(std::string path);
        static s32 numberValidFiles;

        //!Constructor
        ConfigReader();
        //!Destructor
        ~ConfigReader();

        s32 InitSDCard();

        static ConfigReader *instance;

        std::string loadFileToString(std::string path);
        void processFileList(std::vector<std::string> path);

        std::vector<std::string> ScanFolder(std::string path);
};
#endif
