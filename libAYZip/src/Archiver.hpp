//
//  Archiver.hpp
//  AltSign-Windows
//
//  Created by Riley Testut on 8/12/19.
//  Copyright © 2019 Riley Testut. All rights reserved.
//

#ifndef Archiver_hpp
#define Archiver_hpp

#include <string>

bool UnzipAppBundle(const std::string &archivePath, const std::string &outputDirectory);
bool ZipAppBundle(const std::string &appPath, const std::string &archivePath);

#endif /* Archiver_hpp */
