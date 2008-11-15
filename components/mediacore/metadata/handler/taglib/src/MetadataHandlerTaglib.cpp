/*
//
// BEGIN SONGBIRD GPL
//
// This file is part of the Songbird web player.
//
// Copyright(c) 2005-2008 POTI, Inc.
// http://songbirdnest.com
//
// This file may be licensed under the terms of of the
// GNU General Public License Version 2 (the "GPL").
//
// Software distributed under the License is distributed
// on an "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either
// express or implied. See the GPL for the specific language
// governing rights and limitations.
//
// You should have received a copy of the GPL along with this
// program. If not, go to http://www.gnu.org/licenses/gpl.html
// or write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
//
// END SONGBIRD GPL
//
*/

/*******************************************************************************
 *******************************************************************************
 *
 * Taglib metadata handler.
 *
 *******************************************************************************
 ******************************************************************************/

/**
* \file  MetadataHandlerTaglib.cpp
* \brief Songbird MetadataHandlerTaglib implementation.
*/

/*******************************************************************************
 *
 * Taglib metadata handler imported services.
 *
 ******************************************************************************/

/* Local file imports. */
#include "MetadataHandlerTaglib.h"

/* Local module imports. */
#include "TaglibChannelFileIO.h"

/* Songbird utility classes */
#include "sbStringUtils.h"
#include <sbProxiedComponentManager.h>

/* Songbird interfaces */
#include "sbStandardProperties.h"
#include "sbIPropertyArray.h"
#include "sbPropertiesCID.h"

/* Mozilla imports. */
#include <necko/nsIURI.h>
#include <nsComponentManagerUtils.h>
#include <nsIFile.h>
#include <nsIMIMEService.h>
#include <nsIBinaryInputStream.h>
#include <nsIIOService.h>
#include <nsIProtocolHandler.h>
#include <nsIStandardURL.h>
#include <nsICharsetDetector.h>
#include <nsIUTF8ConverterService.h>
#include <nsNetUtil.h>
#include <nsServiceManagerUtils.h>
#include <nsStringGlue.h>
#include <prlog.h>
#include <prprf.h>
#include <unicharutil/nsUnicharUtils.h>
#include <nsMemory.h>

#include <flacfile.h>
#include <mpcfile.h>
#include <mpegfile.h>
#include <urllinkframe.h>
#include <mp4file.h>
#include <vorbisfile.h>

/* C++ std imports. */
#include <sstream>

/* Windows Specific */
#if defined(XP_WIN)
  #include <windows.h>
#endif

#define WRITE_PROPERTY(result, SB_PROPERTY, method)         \
  PR_BEGIN_MACRO                                            \
  result = mpMetadataPropertyArray->GetPropertyValue(       \
    NS_LITERAL_STRING(SB_PROPERTY), propertyValue);         \
  if (NS_SUCCEEDED(result)) {                               \
    f.tag()->set##method(TagLib::String(                    \
      NS_ConvertUTF16toUTF8(propertyValue).BeginReading(),  \
      TagLib::String::UTF8));                               \
  }                                                         \
  PR_END_MACRO

#define WRITE_NUMERIC_PROPERTY(result, SB_PROPERTY, method) \
  PR_BEGIN_MACRO                                            \
  result = mpMetadataPropertyArray->GetPropertyValue(       \
    NS_LITERAL_STRING(SB_PROPERTY), propertyValue);         \
  if (NS_SUCCEEDED(result)) {                               \
    int method;                                             \
    int numRead = PR_sscanf(                                \
      NS_ConvertUTF16toUTF8(propertyValue).BeginReading(),  \
      "%d",                                                 \
      &method);                                             \
    if (numRead == 1) {                                     \
      f.tag()->set##method(method);                         \
    }                                                       \
    else {                                                  \
      f.tag()->set##method(0);                              \
    }                                                       \
  }                                                         \
  PR_END_MACRO

/*******************************************************************************
 *
 * Taglib metadata handler logging services.
 *
 ******************************************************************************/

#ifdef PR_LOGGING
static PRLogModuleInfo* gLog = nsnull;
#define LOG(args) PR_BEGIN_MACRO \
  if (!gLog) { \
    gLog = PR_NewLogModule("sbMetadataHandlerTaglib"); \
  } \
  PR_LOG(gLog, PR_LOG_DEBUG, args); \
  PR_END_MACRO
#else
#define LOG(args)   /* nothing */
#endif


// the minimum number of chracters to feed into the charset detector
#define GUESS_CHARSET_MIN_CHAR_COUNT 256

PRLock* sbMetadataHandlerTaglib::sTaglibLock = nsnull;

/*
 *
 * Taglib metadata handler nsISupports implementation.
 *
 ******************************************************************************/

NS_IMPL_THREADSAFE_ISUPPORTS3(sbMetadataHandlerTaglib, 
                              sbIMetadataHandler,
                              sbISeekableChannelListener,
                              nsICharsetDetectionObserver)


/*******************************************************************************
 *
 * Taglib metadata handler sbIMetadataHandler implementation.
 *
 ******************************************************************************/

/**
* \brief Vote to be the handler returned for the given url
*
* The sbIMetadataManager will instantiate one of every sbIMetadataHandler
* subclass and ask it to vote on the given url.  Whichever handler returns
* the highest vote will be used as the handler for the url.
*
* Values less than zero cause that handler to be ignored.
*
* At the moment, our handlers return -1, 0, or 1 (for "no," "maybe," and
* "yes").
*
* \param url The url upon which one should vote
* \return The vote
* \sa sbIMetadataManager
*/

NS_IMETHODIMP sbMetadataHandlerTaglib::Vote(
    const nsAString             &url,
    PRInt32                     *pVote)
{
    nsString                    _url(url);
    PRInt32                     vote = 0;

    /* Convert the URL to lower case. */
    ToLowerCase(_url);

    /* Check for supported files. */
    if (   (_url.Find(".flac", PR_TRUE) != -1)
        || (_url.Find(".mpc", PR_TRUE) != -1)
        || (_url.Find(".mp3", PR_TRUE) != -1)
        || (_url.Find(".m4a", PR_TRUE) != -1)
        || (_url.Find(".m4p", PR_TRUE) != -1)
        || (_url.Find(".oga", PR_TRUE) != -1)
        || (_url.Find(".wv", PR_TRUE) != -1)
        || (_url.Find(".spx", PR_TRUE) != -1)
        || (_url.Find(".tta", PR_TRUE) != -1)
        || (_url.Find(".oga", PR_TRUE) != -1)
        || (_url.Find(".ogg", PR_TRUE) != -1))
    {
        vote = 1;
    }

    /* Check for unsupported files. */
    else if (   (_url.Find(".avi", PR_TRUE) != -1)
             || (_url.Find(".wma", PR_TRUE) != -1)
             || (_url.Find(".wmv", PR_TRUE) != -1)
             || (_url.Find(".asf", PR_TRUE) != -1)
             || (_url.Find(".wav", PR_TRUE) != -1))
    {
        vote = -1;
    }
    
    /* we can only handle things with mozilla protocol handlers */
    if (vote >= 0) {
        nsresult rv;
        nsCOMPtr<nsIIOService> ios = do_GetIOService(&rv);
        NS_ENSURE_SUCCESS(rv, rv);
        nsCOMPtr<nsIProtocolHandler> protocolHandler;
        nsCString scheme;
        
        rv = ios->ExtractScheme(NS_ConvertUTF16toUTF8(_url), scheme);
        if (NS_SUCCEEDED(rv)) {
            rv = ios->GetProtocolHandler(scheme.BeginReading(),
                                         getter_AddRefs(protocolHandler));
        }
        if (NS_FAILED(rv)) {
            // can't deal with the url
            LOG(("sbMetadataHandlerTaglib::Vote - can't handle scheme %s\n",
                 scheme.BeginReading()));
            vote = -1;
        }
    }

    /* Return results. */
    *pVote = vote;
    LOG(("sbMetadataHandlerTaglib::Vote - voting to %d\n", vote));

    return (NS_OK);
}

NS_IMETHODIMP
sbMetadataHandlerTaglib::GetRequiresMainThread(PRBool *_retval)
{
  NS_ENSURE_ARG_POINTER(_retval);
  NS_ENSURE_STATE(mpChannel);
  nsresult rv;
  
  nsCOMPtr<nsIURI> uri;
  rv = mpChannel->GetURI(getter_AddRefs(uri));
  NS_ENSURE_SUCCESS(rv, rv);
  
  PRBool isFileURI = PR_FALSE;
  rv = uri->SchemeIs( "file" , &isFileURI );
  NS_ENSURE_SUCCESS(rv, rv);

  // Taglib uses the nsIChannel for all protocols
  // other than file:// and cannot be run off
  // of the main thread in these cases.

  *_retval = !isFileURI;
  return NS_OK;
}


/**
* \brief Start the read operation
*
* After getting a handler from the sbIMetadataManager, the user code usually
* calls read upon it, immediately.  The implementor may choose to handle the
* request immediately or asynchronously.
*
* \return -1 if operating asynchronously, otherwise the number of metadata
*            values read (0 on failure)
*/
NS_IMETHODIMP sbMetadataHandlerTaglib::Read(
    PRInt32                     *pReadCount)
{
  nsresult rv = NS_ERROR_FAILURE;
  nsAutoLock lock(sTaglibLock);

  // Attempt to avoid crashes.  This may only work on windows.
  try { 
    rv = ReadInternal(pReadCount); 
  } catch(...) { 
    NS_ERROR("sbMetadataHandlerTaglib::Read caught an exception!");
    rv = NS_ERROR_FAILURE;
  }
  
  return rv;
}

nsresult sbMetadataHandlerTaglib::ReadInternal(
    PRInt32                     *pReadCount)
{
    nsCOMPtr<nsIStandardURL>    pStandardURL;
    nsCOMPtr<nsIURI>            pURI;
    nsCOMPtr<nsIFile>           pFile;
    nsCString                   urlSpec;
    nsCString                   urlScheme;
    nsAutoString                filePath;
    PRUint32                    unsignedReadCount = 0;
    PRInt32                     readCount = 0;
    nsresult                    result = NS_OK;
    
    // Starting a new operation, so clear the completion flag
    mCompleted = PR_FALSE;

    /* Get the TagLib sbISeekableChannel file IO manager. */
    mpTagLibChannelFileIOManager =
            do_GetService
                ("@songbirdnest.com/Songbird/sbTagLibChannelFileIOManager;1",
                 &result);

    /* Initialize the metadata values. */
    if (NS_SUCCEEDED(result))
    {
        mpMetadataPropertyArray = 
          do_CreateInstance(SB_MUTABLEPROPERTYARRAY_CONTRACTID, &result);

        result = mpMetadataPropertyArray->SetStrict(PR_FALSE);
        NS_ENSURE_SUCCESS(result, result);
    }

    /* Get the channel URL info. */
    if (!mpURL) {
      result = NS_ERROR_NOT_INITIALIZED;
    }
    if (NS_SUCCEEDED(result))
        result = mpURL->GetSpec(urlSpec);
    if (NS_SUCCEEDED(result))
        result = mpURL->GetScheme(urlScheme);
    
    LOG(("sbMetadataHandlerTaglib::ReadInternal - spec is %s\n",
         urlSpec.BeginReading()));

    /* If the channel URL scheme is for a local file, try reading  */
    /* synchronously.  If successful, metadata read operation will */
    /* be completed.                                               */
    if (urlScheme.Equals(NS_LITERAL_CSTRING("file")))
    {
        /* Get the metadata local file path. */
        if (NS_SUCCEEDED(result))
        {
            PRBool useSpec = PR_TRUE;
            #if XP_UNIX && !XP_MACOSX
            if (StringBeginsWith(urlSpec, NS_LITERAL_CSTRING("file://"))) {
                nsCString path(Substring(urlSpec, NS_ARRAY_LENGTH("file://") - 1));
                LOG(("looking at path %s\n", path.get()));
                do { /* allow breaking out gracefully */
                    nsCOMPtr<nsILocalFile> localFile =
                        do_CreateInstance("@mozilla.org/file/local;1", &result);
                    if (NS_FAILED(result) || !localFile)
                        break;
                    nsCOMPtr<nsINetUtil> netUtil =
                        do_CreateInstance("@mozilla.org/network/util;1", &result);
                    if (NS_FAILED(result)) {
                        LOG(("failed to get netutil\n"));
                        break;
                    }
                    nsCString unescapedPath;
                    result = netUtil->UnescapeString(path, 0, unescapedPath);
                    if (NS_FAILED(result)) {
                        LOG(("failed to unescape path\n"));
                        break;
                    }
                    result = localFile->SetPersistentDescriptor(unescapedPath);
                    if (NS_FAILED(result)) {
                        LOG(("failed to set persistent descriptor %s\n", unescapedPath.get()));
                        break;
                    }
                    #if PR_LOGGING
                      result = localFile->GetPersistentDescriptor(path);
                      if (NS_SUCCEEDED(result)) {
                        LOG(("file path is %s\n", path.get()));
                      }
                    #endif
                    PRBool fileExists = PR_FALSE;
                    result = localFile->Exists(&fileExists);
                    if (NS_FAILED(result) || !fileExists) {
                        LOG(("file does not exist, falling back"));
                        break;
                    }
                    pFile = do_QueryInterface(localFile, &result);
                    if (NS_SUCCEEDED(result) && pFile)
                        useSpec = PR_FALSE;
                } while (0);
            }
            #endif /* XP_UNIX && !XP_MACOSX */
            if (useSpec)
                result = mpFileProtocolHandler->GetFileFromURLSpec
                                                            (urlSpec,
                                                             getter_AddRefs(pFile));
        }

        if (NS_SUCCEEDED(result)) {
          #if XP_UNIX && !XP_MACOSX
            result = pFile->GetNativePath(mMetadataPath);
          #else
            nsString metadataPathU16;
            result = pFile->GetPath(metadataPathU16);
            if (NS_SUCCEEDED(result)) {
              CopyUTF16toUTF8(metadataPathU16, mMetadataPath);
            }
          #endif
        }
        LOG(("using metadata path %s", mMetadataPath.get()));

        /* Read the metadata. */
        if (NS_SUCCEEDED(result)) {
            result = ReadMetadata();
            // If the read fails, that's it, nothing else to do
            if (NS_FAILED(result)) {
              CompleteRead();
            }
        }
    }

    /* If metadata reading is not complete, start an */
    /* asynchronous read with the metadata channel.  */
    if (NS_SUCCEEDED(result) && !mCompleted)
    {
        /* Create a metadata channel. */
        mpSeekableChannel =
            do_CreateInstance("@songbirdnest.com/Songbird/SeekableChannel;1",
                              &result);

        /* Add the metadata channel for use with the */
        /* TagLib nsIChannel  file I/O services.     */
        if (NS_SUCCEEDED(result))
        {
            /* Allocate a metadata channel ID    */
            /* and use it for the metadata path. */
            PR_AtomicIncrement((PRInt32 *) &sNextChannelID);
            mMetadataPath.AssignLiteral("metadata_channel://");
            mMetadataPath.AppendInt(sNextChannelID);
            mMetadataChannelID = mMetadataPath;

            /* Add the metadata channel. */
            result = mpTagLibChannelFileIOManager->AddChannel
                                                            (mMetadataChannelID,
                                                             mpSeekableChannel);
        }

        /* Open the metadata channel to start reading. */
        if (NS_SUCCEEDED(result))
            mpSeekableChannel->Open(mpChannel, this);

        /* Indicate that reading is being done asynchronously. */
        if (NS_SUCCEEDED(result))
            readCount = -1;
    }

    /* If the read operation is complete, get the number of read tags. */
    if (NS_SUCCEEDED(result) && mCompleted) {
        result = mpMetadataPropertyArray->GetLength(&unsignedReadCount);
        readCount = (PRInt32)unsignedReadCount; // (sigh)
    }

    /* Complete read operation on error. */
    if (!NS_SUCCEEDED(result))
    {
        CompleteRead();
        readCount = 0;
    }

    /* Return results. */
    *pReadCount = readCount;

    return result;
}


/**
* \brief Start the write operation
*
* After getting a handler from the sbIMetadataManager, the user code may set
* an sbIMetadataValues object into the handler and then call write to write
* the abstract metadata map into the specific metadata requirements of the
* file format supported by the handler.
*
* Note that the number of items written may not always equal the number of
* items in the sbIMetadataValues object if the underlying file format does
* not support the given keys.
*
* \return -1 if operating asynchronously, otherwise the number of metadata
*            values written (0 on failure)
*/
NS_IMETHODIMP sbMetadataHandlerTaglib::Write(
    PRInt32                     *pWriteCount)
{
  nsresult rv = NS_ERROR_FAILURE;
  nsAutoLock lock(sTaglibLock);
  // Attempt to avoid crashes.  This may only work on windows.
  try { 
    rv = WriteInternal(pWriteCount);
  } catch(...) { 
    NS_ERROR("sbMetadataHandlerTaglib::Write caught an exception!");
  }

  return rv;
}

nsresult sbMetadataHandlerTaglib::WriteInternal(
    PRInt32                     *pWriteCount)
{
    nsCOMPtr<nsIStandardURL>    pStandardURL;
    nsCOMPtr<nsIURI>            pURI;
    nsCOMPtr<nsIFile>           pFile;
    nsCString                   urlSpec;
    nsCString                   urlScheme;
    nsAutoString                filePath;
    nsresult                    result = NS_OK;
  
    // Starting a new operation, so clear the completion flag
    mCompleted = PR_FALSE;
  
    // Must ensure metadata is set before writing
    NS_ENSURE_TRUE(mpMetadataPropertyArray, NS_ERROR_NOT_INITIALIZED);

    /* Get the TagLib sbISeekableChannel file IO manager. */
    mpTagLibChannelFileIOManager =
            do_GetService
                ("@songbirdnest.com/Songbird/sbTagLibChannelFileIOManager;1",
                 &result);

    /* Get the channel URL info. */
    NS_ENSURE_STATE(mpURL);
    if (NS_SUCCEEDED(result))
        result = mpURL->GetSpec(urlSpec);
    if (NS_SUCCEEDED(result))
        result = mpURL->GetScheme(urlScheme);

    if (!urlScheme.EqualsLiteral("file"))
    {
      return NS_ERROR_NOT_IMPLEMENTED;
    }
     
    /* Get the metadata local file path. */
    if (NS_SUCCEEDED(result))
    {
      result = mpFileProtocolHandler->GetFileFromURLSpec(
        urlSpec, 
        getter_AddRefs(pFile)
      );
    }

    if (NS_SUCCEEDED(result)) {
      #if XP_UNIX && !XP_MACOSX
        result = pFile->GetNativePath(mMetadataPath);
      #else
        nsString metadataPathU16;
        result = pFile->GetPath(metadataPathU16);
        if (NS_SUCCEEDED(result)) {
          CopyUTF16toUTF8(metadataPathU16, mMetadataPath);
        }
      #endif
    }

    /* WRITE the metadata. */
    if (NS_SUCCEEDED(result)) {
      // on windows, we leave this as an nsAString
      // beginreading doesn't promise to return a null terminated string
      // this is RFB
#if XP_WIN
      NS_ConvertUTF8toUTF16 filePath(mMetadataPath);
#else
      nsACString &filePath = mMetadataPath;
#endif

      TagLib::FileRef f(filePath.BeginReading());
      NS_ENSURE_TRUE(f.file(), NS_ERROR_FAILURE);
      NS_ENSURE_TRUE(f.file()->isOpen(), NS_ERROR_FAILURE);
      NS_ENSURE_TRUE(f.file()->isValid(), NS_ERROR_FAILURE);
      
      nsAutoString propertyValue;
      
      // WRITE_PROPERTY is a natty macro
      WRITE_PROPERTY(result, SB_PROPERTY_TRACKNAME, Title);
      WRITE_PROPERTY(result, SB_PROPERTY_ARTISTNAME, Artist);
      WRITE_PROPERTY(result, SB_PROPERTY_ALBUMARTISTNAME, AlbumArtist);
      WRITE_PROPERTY(result, SB_PROPERTY_ALBUMNAME, Album);
      WRITE_PROPERTY(result, SB_PROPERTY_COMMENT, Comment);
      WRITE_PROPERTY(result, SB_PROPERTY_LYRICS, Lyrics);
      WRITE_PROPERTY(result, SB_PROPERTY_GENRE, Genre);
      WRITE_PROPERTY(result, SB_PROPERTY_PRODUCERNAME, Producer);
      WRITE_PROPERTY(result, SB_PROPERTY_COMPOSERNAME, Composer);
      WRITE_PROPERTY(result, SB_PROPERTY_CONDUCTORNAME, Conductor);
      WRITE_PROPERTY(result, SB_PROPERTY_LYRICISTNAME, Lyricist);
      WRITE_PROPERTY(result, SB_PROPERTY_RECORDLABELNAME, RecordLabel);
      WRITE_PROPERTY(result, SB_PROPERTY_RATING, Rating);
      WRITE_PROPERTY(result, SB_PROPERTY_LANGUAGE, Language);
      WRITE_PROPERTY(result, SB_PROPERTY_KEY, Key);
      WRITE_PROPERTY(result, SB_PROPERTY_COPYRIGHT, License);
      WRITE_PROPERTY(result, SB_PROPERTY_COPYRIGHTURL, LicenseUrl);
      WRITE_NUMERIC_PROPERTY(result, SB_PROPERTY_YEAR, Year);
      WRITE_NUMERIC_PROPERTY(result, SB_PROPERTY_TRACKNUMBER, Track);
      WRITE_NUMERIC_PROPERTY(result, SB_PROPERTY_TOTALTRACKS, TotalTracks);
      WRITE_NUMERIC_PROPERTY(result, SB_PROPERTY_DISCNUMBER, Disc);
      WRITE_NUMERIC_PROPERTY(result, SB_PROPERTY_TOTALDISCS, TotalDiscs);
      WRITE_NUMERIC_PROPERTY(result, SB_PROPERTY_BPM, Bpm);
      // todo: isCompilation! + Tests!

      /* Write MP3 specific properties. */
      // TODO: write other files' metadata.
      nsCString                   fileExt;
      result = mpURL->GetFileExtension(fileExt);
      NS_ENSURE_SUCCESS(result, result);
      ToLowerCase(fileExt);
      if (fileExt.Equals(NS_LITERAL_CSTRING("mp3"))) {
        TagLib::MPEG::File* MPEGFile = static_cast<TagLib::MPEG::File*>(f.file());
        
        // Write Image Data
        nsAutoString imageSpec;
        result = mpMetadataPropertyArray->GetPropertyValue(
          NS_LITERAL_STRING(SB_PROPERTY_PRIMARYIMAGEURL),
          imageSpec
        );
        if (NS_SUCCEEDED(result)) {
          PRInt32 imageType = METADATA_IMAGE_TYPE_FRONTCOVER;
          WriteImage(MPEGFile, imageType, imageSpec);
        }
        
        // Look up the origins of this file.
        /* bug 10933: description of a URL is not currently implemented in taglib
        nsAutoString title;
        result = mpMetadataPropertyArray->GetPropertyValue(
          NS_LITERAL_STRING(SB_PROPERTY_ORIGINPAGETITLE),
          title
        );*/
        nsAutoString url;
        result = mpMetadataPropertyArray->GetPropertyValue(
          NS_LITERAL_STRING(SB_PROPERTY_ORIGINPAGE),
          url
        );
        // if we have an origin page
        if (NS_SUCCEEDED(result)) {
          if (MPEGFile->ID3v2Tag()) {
            /*TagLib::String taglibTitle = TagLib::String(
              NS_ConvertUTF16toUTF8(title).BeginReading(),
              TagLib::String::UTF8
            );*/
            
            TagLib::String taglibURL = TagLib::String(
              NS_ConvertUTF16toUTF8(url).BeginReading(),
              TagLib::String::UTF8
            );
            
            // TODO: bug 10932 -- fix WCOP to be like this in TL
            TagLib::ID3v2::Tag* tag = MPEGFile->ID3v2Tag();
            if(taglibURL.isEmpty()) {
              tag->removeFrames("WOAF");
            }
            else if(!tag->frameList("WOAF").isEmpty()) {
              TagLib::ID3v2::UrlLinkFrame* frame =
                static_cast<TagLib::ID3v2::UrlLinkFrame*>(
                  tag->frameList("WOAF").front());
              frame->setUrl(taglibURL);
              //bug 10933: frame->setText(taglibTitle);
            }
            else {
              TagLib::ID3v2::UrlLinkFrame* frame = new TagLib::ID3v2::UrlLinkFrame("WOAF");
              tag->addFrame(frame);
              frame->setUrl(taglibURL);
              //bug 10933: frame->setText(taglibURL);
            }
          }
        }
      }
      
      // Attempt to save the metadata
      if (f.save()) {
        result = NS_OK;
      } else {
        result = NS_ERROR_FAILURE;      
      }
    }
    
    // TODO need to set pWriteCount

    // Indicate that the operation is complete
    mCompleted = PR_TRUE;
  
    return result;
}

/**
* \brief Extracts an image from the medatada
*
* This runs independent of the normal read operation so it will not fill in
* the properties with the other tag information. It also operates on file scheme
* uris only (local files), and works synchronously. Currently only id3 tags are
* implemented.
*
* \param aType const type for the image to retrieve (see METADATA_IMAGE_TYPE*)
* \param aMimeType Output parameter for mimetype of the image
* \param aDataLen Output parameter for length of image data
* \param aData Output parameter for binary data of image
*/
NS_IMETHODIMP sbMetadataHandlerTaglib::GetImageData(
    PRInt32       aType,
    nsACString    &aMimeType,
    PRUint32      *aDataLen,
    PRUint8       **aData)
{
  nsresult rv;
  NS_ENSURE_ARG_POINTER(aData);
  
  LOG(("sbMetadataHandlerTaglib::GetImageData\n"));

  // First check to see if we have cached art for
  // the requested type.  This is an optimization
  // to avoid reading metadata twice, or otherwise
  // locking taglib on the main thread.
  sbAlbumArt *cachedArt = nsnull;
  for (PRUint32 i=0; i < mCachedAlbumArt.Length(); i++) {
    cachedArt = mCachedAlbumArt[i];
    NS_ENSURE_TRUE(cachedArt, NS_ERROR_UNEXPECTED);
    if (cachedArt->type == aType) {
      LOG(("sbMetadataHandlerTaglib::GetImageData - found cached image\n"));
  
      aMimeType.Assign(cachedArt->mimeType);
      *aDataLen = cachedArt->dataLen;
      *aData = cachedArt->data;

      // The data now belongs to the caller, so we
      // are not allowed to free it or use it again.
      cachedArt->dataLen = 0;
      cachedArt->data = nsnull;
      mCachedAlbumArt.RemoveElementAt(i);

      return NS_OK;
    }
  }

  // If read has already been run, then we can assume 
  // that all images should have been in the cache.
  // Since we didn't find any in the cache, just go 
  // ahead and return null.
  if (mCompleted) {
    *aDataLen = 0;
    *aData = nsnull;
    return NS_OK;
  }

  // Nothing was found in the cache, so open the target file
  // and read the data out manually.

  nsAutoLock lock(sTaglibLock);
  try { 
    rv = GetImageDataInternal(aType, aMimeType, aDataLen, aData);
  } catch(...) { 
    NS_ERROR("sbMetadataHandlerTaglib::GetImageData caught an exception!");
    rv = NS_ERROR_FAILURE;
  }
  return rv;
}

nsresult sbMetadataHandlerTaglib::GetImageDataInternal(
    PRInt32       aType,
    nsACString    &aMimeType,
    PRUint32      *aDataLen,
    PRUint8       **aData)
{
  nsCOMPtr<nsIFile>           pFile;
  nsCString                   urlSpec;
  nsCString                   urlScheme;
  nsCString                   fileExt;
  nsresult                    result = NS_OK;

  /* Get the channel URL info. */
  NS_ENSURE_STATE(mpURL);
  result = mpURL->GetSpec(urlSpec);
  NS_ENSURE_SUCCESS(result, result);
  result = mpURL->GetScheme(urlScheme);
  NS_ENSURE_SUCCESS(result, result);
 
  if (urlScheme.EqualsLiteral("file"))
  {
    /* Get the metadata file extension. */
    result = mpURL->GetFileExtension(fileExt);
    NS_ENSURE_SUCCESS(result, result);
    ToLowerCase(fileExt);
  
    if (!fileExt.Equals(NS_LITERAL_CSTRING("mp3"))) {
      return NS_ERROR_NOT_IMPLEMENTED;
    }
   
    /* Get the metadata local file path. */
    result = mpFileProtocolHandler->GetFileFromURLSpec
                                                (urlSpec,
                                                 getter_AddRefs(pFile));
    NS_ENSURE_SUCCESS(result, result);
  
#if XP_WIN
    nsString filePath;
    result = pFile->GetPath(filePath);
    NS_ENSURE_SUCCESS(result, result);
    CopyUTF16toUTF8(filePath, mMetadataPath);
#else
    result = pFile->GetNativePath(mMetadataPath);
    NS_ENSURE_SUCCESS(result, result);
    nsCString filePath = mMetadataPath;
#endif

    nsAutoPtr<TagLib::MPEG::File> pTagFile;
    pTagFile = new TagLib::MPEG::File(filePath.BeginReading());    
    NS_ENSURE_STATE(pTagFile);

    /* Read the metadata file. */
    if (pTagFile->ID3v2Tag()) {
      result = ReadImage(pTagFile->ID3v2Tag(), aType, aMimeType, aDataLen, aData);
    } else {
      result = NS_ERROR_FILE_UNKNOWN_TYPE;
    }
  } else {
    result = NS_ERROR_NOT_IMPLEMENTED;
  }

  return result;
}

NS_IMETHODIMP sbMetadataHandlerTaglib::SetImageData(
    PRInt32             aType,
    const nsAString     &aURL)
{
  nsresult rv;
  LOG(("sbMetadataHandlerTaglib::SetImageData\n"));
  nsAutoLock lock(sTaglibLock);

  try { 
    rv = SetImageDataInternal(aType, aURL);
  } catch(...) { 
    NS_ERROR("sbMetadataHandlerTaglib::SetImageData caught an exception!");
    rv = NS_ERROR_FAILURE;
  }
  return rv;
}

nsresult sbMetadataHandlerTaglib::SetImageDataInternal(
    PRInt32             aType,
    const nsAString     &aURL)
{
  nsCOMPtr<nsIFile>           pFile;
  nsCString                   urlSpec;
  nsCString                   urlScheme;
  nsCString                   fileExt;
  nsresult                    result = NS_OK;

  NS_ENSURE_STATE(mpURL);

  // TODO: write other files' metadata.
  // First check if we support this file
  result = mpURL->GetFileExtension(fileExt);
  NS_ENSURE_SUCCESS(result, result);
  
  ToLowerCase(fileExt);
  if (!fileExt.Equals(NS_LITERAL_CSTRING("mp3"))) {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  result = mpURL->GetSpec(urlSpec);
  NS_ENSURE_SUCCESS(result, result);
  result = mpURL->GetScheme(urlScheme);
  NS_ENSURE_SUCCESS(result, result);

  if (urlScheme.EqualsLiteral("file"))
  {
    /* Get the metadata local file path. */
    result = mpFileProtocolHandler->GetFileFromURLSpec
                                                (urlSpec,
                                                 getter_AddRefs(pFile));
    NS_ENSURE_SUCCESS(result, result);
  
#if XP_WIN
    nsString filePath;
    result = pFile->GetPath(filePath);
    NS_ENSURE_SUCCESS(result, result);
    CopyUTF16toUTF8(filePath, mMetadataPath);
#else
    result = pFile->GetNativePath(mMetadataPath);
    NS_ENSURE_SUCCESS(result, result);
    nsCString filePath = mMetadataPath;
#endif

    /* Open and read the metadata file. */
    TagLib::FileRef f(filePath.BeginReading());
    NS_ENSURE_TRUE(f.file(), NS_ERROR_FAILURE);
    NS_ENSURE_TRUE(f.file()->isOpen(), NS_ERROR_FAILURE);
    NS_ENSURE_TRUE(f.file()->isValid(), NS_ERROR_FAILURE);
    result = WriteImage(static_cast<TagLib::MPEG::File*>(f.file()),
                        aType,
                        aURL);
    // WriteSetImageDataInternal does not write the metadata to file
    if (NS_SUCCEEDED(result)) {
      // Attempt to save the metadata
      if (f.save()) {
        result = NS_OK;
      } else {
        result = NS_ERROR_FAILURE;
      } 
    }
  } else {
    result = NS_ERROR_NOT_IMPLEMENTED;
  }
  
  return result;
}

/**
 * \brief RemoveAllImages - Removes all of the <MPEGFile>'s images of type
 *                    <imageType>.
 * \param aMPEGFile - the TagLib::MPEG::File pointer
 * \param imageType - an enum from determining the type of image like
 *                    sbIMetadataHandler::METADATA_IMAGE_TYPE_FRONTCOVER
 */
nsresult sbMetadataHandlerTaglib::RemoveAllImages(TagLib::MPEG::File* aMPEGFile,
                                                  PRInt32 imageType)
{
  if (aMPEGFile->ID3v2Tag()) {
    TagLib::ID3v2::FrameList frameList= aMPEGFile->ID3v2Tag()->frameList("APIC");
    if (!frameList.isEmpty()){
      std::list<TagLib::ID3v2::Frame*>::const_iterator iter = frameList.begin();
      while (iter != frameList.end()) {
        TagLib::ID3v2::AttachedPictureFrame *frame =
                    static_cast<TagLib::ID3v2::AttachedPictureFrame *>( *iter );
        std::list<TagLib::ID3v2::Frame*>::const_iterator nextIter = iter;
        nextIter++;
        if (frame && frame->type() == imageType){
          // Remove and free the memory for this frame
          aMPEGFile->ID3v2Tag()->removeFrame(*iter, true);
        }
        iter = nextIter;
      }
    }
  }
  
  return NS_OK;
}

/**
 * \brief WriteImage - set the <MPEGFile>'s image of type
 *                    <imageType> to the contents of the file at <imageSpec>.
 * \param aMPEGFile - the TagLib::MPEG::File pointer
 * \param imageType - an enum from determining the type of image like
 *                    sbIMetadataHandler::METADATA_IMAGE_TYPE_FRONTCOVER
 * \param imageSpec - the string with the URL of the file, use "" if you wish
 *                    to remove all images of <imageType> from the metadata.
 */
nsresult sbMetadataHandlerTaglib::WriteImage(TagLib::MPEG::File* aMPEGFile,
                                             PRInt32 imageType,
                                             const nsAString &imageSpec)
{
  nsresult rv;
  
  if (!aMPEGFile->ID3v2Tag()) {
    // Not ID3v2 tag then abort
    return NS_ERROR_FAILURE;
  }
  
  if (imageSpec.Length() > 0) {
    nsCOMPtr<nsIFile> imageFile;
    nsCString imageMimeType;
    PRUint8*  imageData;
    PRUint32  imageDataSize;
    
    // open the image file
    rv = mpFileProtocolHandler->GetFileFromURLSpec(
            NS_LossyConvertUTF16toASCII(imageSpec), 
            getter_AddRefs(imageFile)
    );
    NS_ENSURE_SUCCESS(rv, rv);
  
    // Get the mime type.
    nsCOMPtr<nsIMIMEService> mimeService =
                                do_GetService("@mozilla.org/mime;1", &rv);
    NS_ENSURE_SUCCESS(rv, rv);
    rv = mimeService->GetTypeFromFile(imageFile, imageMimeType);
    NS_ENSURE_SUCCESS(rv, rv);
  
    // Load the actual data.
    nsCOMPtr<nsIFileInputStream> inputStream =
       do_CreateInstance("@mozilla.org/network/file-input-stream;1", &rv);
    NS_ENSURE_SUCCESS(rv, rv);
    
    rv = inputStream->Init(imageFile, 0x01, 0600, 0);
    NS_ENSURE_SUCCESS(rv, rv);
    
    nsCOMPtr<nsIBinaryInputStream> stream =
               do_CreateInstance("@mozilla.org/binaryinputstream;1", &rv);
    NS_ENSURE_SUCCESS(rv, rv);
    
    rv = stream->SetInputStream(inputStream);
    NS_ENSURE_SUCCESS(rv, rv);
    
    // First length,
    rv = inputStream->Available(&imageDataSize);
    NS_ENSURE_SUCCESS(rv, rv);
    
    // then the data itself
    rv = stream->ReadByteArray(imageDataSize, &imageData);
    NS_ENSURE_SUCCESS(rv, rv);

    // Create the picture frame, and set mimetype, image type (e.g. front cover)
    // and then fill in the data.
    TagLib::ID3v2::AttachedPictureFrame *pic =
                                        new TagLib::ID3v2::AttachedPictureFrame;
    pic->setMimeType(TagLib::String(imageMimeType.BeginReading(),
                                    TagLib::String::UTF8));
    pic->setType(TagLib::ID3v2::AttachedPictureFrame::Type(imageType));
    pic->setPicture(TagLib::ByteVector((const char *)imageData, imageDataSize));

    // First we have to remove any other existing frames of the same type
    rv = RemoveAllImages(aMPEGFile, imageType);
    NS_ENSURE_SUCCESS(rv, rv);

    // Add the frame to the file.
    aMPEGFile->ID3v2Tag()->addFrame(pic);
    
  } else {
    // No image provided so just remove existing ones.
    rv = RemoveAllImages(aMPEGFile, imageType);
  }
  
  return rv;
}

/**
 * \brief Extracts an image from the given mpeg file
 * \param aTag - the ID3v2 tags
 * \param imageType - an enum from determining the type of image like
 *                    sbIMetadataHandler::METADATA_IMAGE_TYPE_FRONTCOVER
 * \param aDataLen Output parameter for length of image data
 * \param aData Output parameter for binary data of image
 */
nsresult sbMetadataHandlerTaglib::ReadImage(TagLib::ID3v2::Tag          *aTag,
                                            PRInt32                     aType,
                                            nsACString                  &aMimeType,
                                            PRUint32                    *aDataLen,
                                            PRUint8                     **aData)
{
  NS_ENSURE_ARG_POINTER(aTag);
  NS_ENSURE_ARG_POINTER(aData);
  nsresult rv = NS_OK;
  
  if (!aTag) {
    // Not ID3v2 tag then abort
    return NS_ERROR_FAILURE;
  }
  
  /*
   * Extract the requested image from the metadata
   */
  TagLib::ID3v2::FrameList frameList = aTag->frameList("APIC");
  if (!frameList.isEmpty()){
    TagLib::ID3v2::AttachedPictureFrame *p = nsnull;
    for (TagLib::uint frameIndex = 0;
         frameIndex < frameList.size();
         frameIndex++) {
      p =  static_cast<TagLib::ID3v2::AttachedPictureFrame *>(frameList[frameIndex]);
      if (p->type() == aType && p->picture().size() > 0) {
        // Store the size of the data
        *aDataLen = p->picture().size();
        // Store the mimeType acquired from the image data
        // these can sometimes be in a format like "PNG"
        aMimeType.Assign(p->mimeType().toCString(), p->mimeType().length());
        
        // Copy the data over to a mozilla memory chunk so we don't break
        // Things :).
        *aData = static_cast<PRUint8 *>(nsMemory::Clone(p->picture().data(),
                                                        *aDataLen));
        NS_ENSURE_TRUE(*aData, NS_ERROR_OUT_OF_MEMORY);
        break;
      }
    }
  }
  
  return rv;
}


/**
* \brief Be thou informst that one's sbIMetadataChannel has just received data
*
* Every time the underlying nsIChannel dumps data on the sbIMetadataChannel,
* plus once more for when the nsIChannel reports a stop condition.
*
* This is a chance for the handler code to attempt to parse the datastream.
*
* \param pChannel The sbIMetadataChannel delivering data.  You'll have to QI for
*                 it.
*/

NS_IMETHODIMP sbMetadataHandlerTaglib::OnChannelData(
    nsISupports                 *pChannel)
{
    return (NS_OK);
}


/**
* \brief Close down the internals of the handler, stop any downloads, free any
*        allocations
*/

NS_IMETHODIMP sbMetadataHandlerTaglib::Close()
{
    /* Throw away cached album art */
    mCachedAlbumArt.Clear();
    
    /* If a metadata channel is being used, remove it from */
    /* use with the TagLib nsIChannel file I/O services.   */
    if (!mMetadataChannelID.IsEmpty())
    {
        mpTagLibChannelFileIOManager->RemoveChannel(mMetadataChannelID);
        mMetadataChannelID.Truncate();
    }

    /* Close the metadata channel. */
    if (mpSeekableChannel)
    {
        mpSeekableChannel->Close();
        mpSeekableChannel = nsnull;
    }

    /* Complete read operation. */
    CompleteRead();

    return (NS_OK);
}


/*
 * Getters/setters
 */

NS_IMETHODIMP sbMetadataHandlerTaglib::GetProps(
    sbIMutablePropertyArray     **ppPropertyArray)
{
    NS_ENSURE_ARG_POINTER(ppPropertyArray);
    NS_ENSURE_STATE(mpMetadataPropertyArray);
    NS_ADDREF(*ppPropertyArray = mpMetadataPropertyArray);
    return (NS_OK);
}

NS_IMETHODIMP sbMetadataHandlerTaglib::SetProps(
    sbIMutablePropertyArray     *ppPropertyArray)
{
    mpMetadataPropertyArray = ppPropertyArray;
    return (NS_OK);
}

NS_IMETHODIMP sbMetadataHandlerTaglib::GetCompleted(
    PRBool                      *pCompleted)
{
    NS_ENSURE_ARG_POINTER(pCompleted);
    *pCompleted = mCompleted;
    return (NS_OK);
}

NS_IMETHODIMP sbMetadataHandlerTaglib::GetChannel(
    nsIChannel                  **ppChannel)
{
    NS_ENSURE_ARG_POINTER(ppChannel);
    NS_IF_ADDREF(*ppChannel = mpChannel);
    return (NS_OK);
}

NS_IMETHODIMP sbMetadataHandlerTaglib::SetChannel(
    nsIChannel                  *pChannel)
{
    mpChannel = pChannel;
    if (!mpChannel) {
      mpURL = nsnull;
    } else {
      /* Get the channel URL info. */
      nsCOMPtr<nsIURI>            pURI;
      nsresult                    result = NS_OK;
      result = mpChannel->GetURI(getter_AddRefs(pURI));
      NS_ENSURE_SUCCESS(result, result);
      mpURL = do_QueryInterface(pURI, &result);
      NS_ENSURE_SUCCESS(result, result);
    }
    
    return (NS_OK);
}


/*******************************************************************************
 *
 * Taglib metadata handler sbISeekableChannel implementation.
 *
 ******************************************************************************/

/**
* \brief Be thou informst that one's sbISeekableChannel has just received data
*
* Every time the underlying nsIChannel dumps data on the sbISeekableChannel,
* plus once more for when the nsIChannel reports a stop condition.
*
* This is a chance for the handler code to attempt to parse the datastream.
*
* \param pChannel The sbISeekableChannel delivering data.  You'll have to QI for
*                 it.
*/

NS_IMETHODIMP sbMetadataHandlerTaglib::OnChannelDataAvailable(
    sbISeekableChannel          *pChannel)
{
    PRBool                      channelCompleted;
    nsresult                    result = NS_OK;

    /* Do nothing if the metadata reading is complete. */
    if (mCompleted)
        return (result);

    /* Process channel data and complete processing on any exception. */
    try
    {
        /* Clear channel restart condition. */
        mMetadataChannelRestart = PR_FALSE;

        /* Read the metadata. */
        {
          nsAutoLock lock(sTaglibLock);
          ReadMetadata();
        }

        /* Check for metadata channel completion. */
        if (!mCompleted)
        {
            result = mpSeekableChannel->GetCompleted(&channelCompleted);
            if (NS_SUCCEEDED(result) && channelCompleted)
                CompleteRead();
        }
    }
    catch(...)
    {
        printf("1: OnChannelDataAvailable exception\n");
        CompleteRead();
    }

    return (result);
}


/*******************************************************************************
 *
 * Public taglib metadata handler services.
 *
 ******************************************************************************/

/*
 * sbMetadataHandlerTaglib
 *
 *   This method is the constructor for the taglib metadata handler class.
 */

sbMetadataHandlerTaglib::sbMetadataHandlerTaglib()
:
    mpTagLibChannelFileIOManager(nsnull),
    mpFileProtocolHandler(nsnull),
    mpMetadataPropertyArray(nsnull),
    mpChannel(nsnull),
    mpSeekableChannel(nsnull),
    mpURL(nsnull),
    mMetadataChannelRestart(PR_FALSE),
    mCompleted(PR_FALSE)
{
}


/*
 * ~sbMetadataHandlerTaglib
 *
 *   This method is the destructor for the taglib metadata handler class.
 */

sbMetadataHandlerTaglib::~sbMetadataHandlerTaglib()
{
    /* Ensure the taglib metadata handler is closed. */
    Close();
}


/*
 * FactoryInit
 *
 *   This function is called by the component factory to initialize the
 * component.
 */

nsresult sbMetadataHandlerTaglib::Init()
{
    nsresult                    rv;

    /* Get the file protocol handler now (since this is mostly threadsafe,
       whereas the IO service definitely isn't) */
    nsCOMPtr<nsIIOService> ioservice =
      do_GetService("@mozilla.org/network/io-service;1", &rv);
    NS_ENSURE_SUCCESS(rv, rv);
    nsCOMPtr<nsIProtocolHandler> fileHandler;
    rv = ioservice->GetProtocolHandler("file", getter_AddRefs(fileHandler));
    NS_ENSURE_SUCCESS(rv, rv);
    mpFileProtocolHandler = do_QueryInterface(fileHandler, &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    mProxiedServices =
      do_GetService("@songbirdnest.com/moz/proxied-services;1", &rv);
    NS_ENSURE_SUCCESS(rv, rv);

    return (NS_OK);
}

/* static */
nsresult sbMetadataHandlerTaglib::ModuleConstructor(nsIModule* aSelf)
{
  sbMetadataHandlerTaglib::sTaglibLock =
    nsAutoLock::NewLock("sbMetadataHandlerTaglib::sTaglibLock");
  NS_ENSURE_TRUE(sbMetadataHandlerTaglib::sTaglibLock, NS_ERROR_OUT_OF_MEMORY);

  return NS_OK;
}

/* static */
void sbMetadataHandlerTaglib::ModuleDestructor(nsIModule* aSelf)
{
  if (sbMetadataHandlerTaglib::sTaglibLock) {
    nsAutoLock::DestroyLock(sbMetadataHandlerTaglib::sTaglibLock);
  }
}

/*******************************************************************************
 *
 * Private taglib metadata handler class services.
 *
 ******************************************************************************/

/*
 * Static field initializers.
 */

PRUint32 sbMetadataHandlerTaglib::sNextChannelID = 0;


/*******************************************************************************
 *
 * Private taglib metadata handler ID3v2 services.
 *
 ******************************************************************************/

/*
 * ID3v2Map                     Map of ID3v2 tag names to Songbird metadata
 *                              names.
 */

static const char *ID3v2Map[][2] =
{
    { "TENC", SB_PROPERTY_SOFTWAREVENDOR },
    { "TIT3", SB_PROPERTY_SUBTITLE },
//    { "TMED", "mediatype" },
//    { "TOAL", "original_album" },
//    { "TOPE", "original_artist" },
//    { "TORY", "original_year" },
//    { "TPE2", "accompaniment" },
//    { "TPE4", "interpreter_remixer" },
    { "UFID", SB_PROPERTY_METADATAUUID },
//    { "USER", "terms_of_use" },
//    { "WCOM", "commercialinfo_url" },
//    { "WOAR", "artist_url" },
//    { "WOAS", "source_url" },
//    { "WORS", "netradio_url" },
//    { "WPAY", "payment_url" },
//    { "WPUB", "publisher_url" },
//    { "WXXX", "user_url" }
      { "XXXX", "junq" }
};


/*
 * ReadID3v2Tags
 *
 *   --> pTag                   ID3v2 tag object.
 *   --> aCharset               The character set the tags are assumed to be in
 *                              Pass in null to do no conversion
 *
 *   This function reads ID3v2 tags from the ID3v2 tag object specified by pTag.
 * The read tags are added to the set of metadata values.
 */

void sbMetadataHandlerTaglib::ReadID3v2Tags(
  TagLib::ID3v2::Tag          *pTag,
  const char                  *aCharset)
{
  TagLib::ID3v2::FrameListMap frameListMap;
  int                         numMetadataEntries;
  int                         i;

  /* Do nothing if no tags are present. */
  if (!pTag) {
      return;
  }

  frameListMap = pTag->frameListMap();

  /* Add the metadata entries. */
  numMetadataEntries = sizeof(ID3v2Map) / sizeof(ID3v2Map[0]);
  for (i = 0; i < numMetadataEntries; i++) {
    TagLib::ID3v2::FrameList frameList = frameListMap[ID3v2Map[i][0]];
    if(!frameList.isEmpty()) {
      TagLib::String value = ConvertCharset(frameList.front()->toString(), aCharset);
      AddMetadataValue(ID3v2Map[i][1], value);
    }
  }

  // TODO: bug 10932 -- make WCOP like this in taglib
  TagLib::ID3v2::FrameList frameList = frameListMap["WOAF"];
  if (!frameList.isEmpty())
  {
    TagLib::ID3v2::UrlLinkFrame* woaf =
      static_cast<TagLib::ID3v2::UrlLinkFrame*>(frameList.front());
    TagLib::String taglibURL = woaf->url();
    
    AddMetadataValue(SB_PROPERTY_ORIGINPAGE,
                     ConvertCharset(taglibURL, aCharset));
    /* bug 10933 -- UrlLinkFrame does not support setText appropriately
    TagLib::String taglibTitle = woaf->text();
    AddMetadataValue(SB_PROPERTY_ORIGINPAGETITLE,
                     ConvertCharset(taglibTitle, aCharset));*/
  }
  
  // If this is a local file, cache common album art in order to speed 
  // up any subsequent calls to GetImageData.
  nsCString urlScheme;
  nsresult result = mpURL->GetScheme(urlScheme);
  NS_ENSURE_SUCCESS(result,/*void*/);
  
  if (urlScheme.Equals(NS_LITERAL_CSTRING("file"))) { 
    sbAlbumArt *art = new sbAlbumArt();
    NS_ENSURE_TRUE(art,/*void*/);  
    result = ReadImage(pTag, sbIMetadataHandler::METADATA_IMAGE_TYPE_FRONTCOVER,
                       art->mimeType, &(art->dataLen), &(art->data));
    NS_ENSURE_SUCCESS(result,/*void*/);
    art->type = sbIMetadataHandler::METADATA_IMAGE_TYPE_FRONTCOVER;
    nsAutoPtr<sbAlbumArt>* cacheSlot = mCachedAlbumArt.AppendElement();
    NS_ENSURE_TRUE(cacheSlot,/*void*/);
    *cacheSlot = art;
  
    art = new sbAlbumArt();
    NS_ENSURE_TRUE(art,/*void*/);  
    result = ReadImage(pTag, sbIMetadataHandler::METADATA_IMAGE_TYPE_OTHER,
                       art->mimeType, &(art->dataLen), &(art->data));
    NS_ENSURE_SUCCESS(result,/*void*/);
    art->type = sbIMetadataHandler::METADATA_IMAGE_TYPE_OTHER;
    cacheSlot = mCachedAlbumArt.AppendElement();
    NS_ENSURE_TRUE(cacheSlot,/*void*/);
    *cacheSlot = art;
  }
  
  return;
}


/*******************************************************************************
 *
 * Private taglib metadata handler APE services.
 *
 ******************************************************************************/

/*
 * APEMap                     Map of APE tag names to Songbird metadata
 *                            names.
 */

// see http://wiki.hydrogenaudio.org/index.php?title=APE_key
static const char *APEMap[][2] =
{
    { "Subtitle", SB_PROPERTY_SUBTITLE },
//    { "Debut album", "original_album" },
//    { "File", "source_url" },
};


/*
 * ReadAPETags
 *
 *   --> pTag                   APE tag object.
 *
 *   This function reads APE tags from the APE tag object specified by pTag.
 * The read tags are added to the set of metadata values.
 */

void sbMetadataHandlerTaglib::ReadAPETags(
    TagLib::APE::Tag          *pTag)
{
    TagLib::APE::ItemListMap  itemListMap;
    int                       numMetadataEntries;
    int                       i;

    /* Do nothing if no tags are present. */
    if (!pTag) {
        return;
    }

    /* Get the item list map. */
    itemListMap = pTag->itemListMap();

    /* Add the metadata entries. */
    numMetadataEntries = sizeof(APEMap) / sizeof(APEMap[0]);
    for (i = 0; i < numMetadataEntries; i++) {
      TagLib::APE::Item item = itemListMap[APEMap[i][0]];
      if (!item.isEmpty()) {
        AddMetadataValue(APEMap[i][1], item.toString());
      }
    }
}


/*******************************************************************************
 *
 * Private taglib metadata handler Xiph services.
 *
 ******************************************************************************/

/*
 * ReadXiphTags
 *
 *   --> pTag                   Xiph tag object.
 *
 *   This function reads Xiph tags from the Xiph tag object specified by pTag.
 * The read tags are added to the set of metadata values.
 */

void sbMetadataHandlerTaglib::ReadXiphTags(
    TagLib::Ogg::XiphComment    *pTag)
{
  // nothing to do here. all the xiph properties we're interested in are already being read
}

/*******************************************************************************
 *
 * Private taglib metadata handler services.
 *
 ******************************************************************************/


 /*
  * OpenTagFile
  *
  *   Open and configure the given file instance for
  * the specified URL.
  */

 nsresult sbMetadataHandlerTaglib::OpenTagFile(TagLib::File *pTagFile)
 {
     NS_ENSURE_ARG_POINTER(pTagFile);

     /* Get the file path in the proper format for the platform. */
 #if XP_WIN
     NS_ConvertUTF8toUTF16 filePath(mMetadataPath);
 #else
     nsACString &filePath = mMetadataPath;
 #endif

     pTagFile->setMaxScanBytes(MAX_SCAN_BYTES);
     pTagFile->open(filePath.BeginReading());

     return NS_OK;
 }


 /*
  * CheckChannelRestart
  *
  *   Determine if the channel is being restarted, or if
  * it has failed in some way.
  */

 nsresult sbMetadataHandlerTaglib::CheckChannelRestart()
 {
     nsresult result = NS_OK;

     if (!mMetadataChannelID.IsEmpty())
     {
         result =
             mpTagLibChannelFileIOManager->GetChannelRestart(mMetadataChannelID,
                                                             &mMetadataChannelRestart);
         NS_ENSURE_SUCCESS(result, result);
         if (!mMetadataChannelRestart) {
             PRUint64 size;
             result = mpTagLibChannelFileIOManager->GetChannelSize(mMetadataChannelID,
                                                                   &size);
             NS_ENSURE_SUCCESS(result, result);

             // we don't consider empty files to be valid because no data
             // could have been read.  Taglib considers files valid by default
             // so we can't actually ask it for anything useful (that still
             // manages to separate the "read error" and "validly no tags" cases)
             if (size <= 0) {
                 result = NS_ERROR_FAILURE;
             }
         }
     }

     return (result);
 }

/*
 * ReadMetadata
 *
 *   This function attempts to read the metadata.  If the metadata is available,
 * this function extracts the metadata values and sets the metadata reading
 * complete condition to true.
 */

nsresult sbMetadataHandlerTaglib::ReadMetadata()
{
    nsCString                   fileExt;
    PRBool                      isValid = PR_FALSE;
    PRBool                      decodedFileExt = PR_FALSE;
    nsresult                    result = NS_OK;

    /* Get the metadata file extension. */
    result = mpURL->GetFileExtension(fileExt);
    if (NS_SUCCEEDED(result))
        ToLowerCase(fileExt);

    #if PR_LOGGING
    {
      nsCString spec;
      result = mpURL->GetSpec(spec);
      if (NS_SUCCEEDED(result)) {
        LOG(("sbMetadataHandlerTaglib:: Reading metadata from %s\n", spec.get()));
      }
    }
    #endif /* PR_LOGGING */

    /* Read the metadata using the file extension */
    /* to determine the metadata format.          */
    if (NS_SUCCEEDED(result))
    {
        decodedFileExt = PR_TRUE;
        if (fileExt.Equals(NS_LITERAL_CSTRING("flac"))) {            
            isValid = ReadFLACFile();
        } else if (fileExt.Equals(NS_LITERAL_CSTRING("mpc"))) {
            isValid = ReadMPCFile();
        } else if (fileExt.Equals(NS_LITERAL_CSTRING("mp3"))) {
            isValid = ReadMPEGFile();
        } else if (fileExt.Equals(NS_LITERAL_CSTRING("m4a"))) {
            isValid = ReadMP4File();
        } else if (fileExt.Equals(NS_LITERAL_CSTRING("m4p"))) {
            isValid = ReadMP4File();
        } else if (fileExt.Equals(NS_LITERAL_CSTRING("ogg"))) {
            isValid = ReadOGGFile();
        } else if (fileExt.Equals(NS_LITERAL_CSTRING("oga"))) {
            isValid = ReadOGGFile();
        } else if (fileExt.Equals(NS_LITERAL_CSTRING("ogv"))) {
            isValid = ReadOGGFile();
        } else if (fileExt.Equals(NS_LITERAL_CSTRING("ogm"))) {
            isValid = ReadOGGFile();
        } else if (fileExt.Equals(NS_LITERAL_CSTRING("ogx"))) {
            isValid = ReadOGGFile();
        } else {
            decodedFileExt = PR_FALSE;
        }
    }

    /* If the file extension was not decoded, try */
    /* reading the metadata in different formats. */
    if (   NS_SUCCEEDED(result)
        && !decodedFileExt
        && !isValid
        && !mMetadataChannelRestart)
    {
        isValid = ReadMPEGFile();
    }

    /* Check if the metadata reading is complete. */
    if (isValid && !mMetadataChannelRestart)
        CompleteRead();

    /* Complete metadata reading upon error. */
    if (!NS_SUCCEEDED(result))
        CompleteRead();

    /* If we weren't able to read, make sure we return a failure code */
    if (!isValid) {
      result = NS_ERROR_FAILURE;
    }

    return (result);
}


/*
 * CompleteRead
 *
 *   This function completes a metadata read operation.
 */

void sbMetadataHandlerTaglib::CompleteRead()
{
    /* Indicate that the metadata read operation has completed. */
    mCompleted = PR_TRUE;
}


/*
 * ReadFile
 *
 *   --> pTagFile               File from which to read.
 *   --> aCharset               The character encoding to use for reading
 *                              May be empty string (to mean no conversion)
 *                              On Windows, may also be "CP_ACP"
 *
 *   <--                        True if file has valid metadata.
 *
 *   This function reads a base set of metadata from the file specified by
 * pTagFile.
 */

PRBool sbMetadataHandlerTaglib::ReadFile(
    TagLib::File                *pTagFile,
    const char                  *aCharset)
{
  TagLib::Tag                 *pTag;
  TagLib::AudioProperties     *pAudioProperties;

  /* We want to be sure we have a legit file ref. */
  if (!pTagFile || !pTagFile->isValid()) {
    return false; // not valid!
  }
  
  pTag = pTagFile->tag();
  if (pTag) {
    // yay random charset guessing!
    AddMetadataValue(SB_PROPERTY_TRACKNAME,       ConvertCharset(pTag->title(), aCharset));
    AddMetadataValue(SB_PROPERTY_ARTISTNAME,      ConvertCharset(pTag->artist(), aCharset));
    AddMetadataValue(SB_PROPERTY_ALBUMARTISTNAME, ConvertCharset(pTag->albumArtist(), aCharset));
    AddMetadataValue(SB_PROPERTY_ALBUMNAME,       ConvertCharset(pTag->album(), aCharset));
    AddMetadataValue(SB_PROPERTY_COMMENT,         ConvertCharset(pTag->comment(), aCharset));
    AddMetadataValue(SB_PROPERTY_LYRICS,          ConvertCharset(pTag->lyrics(), aCharset));
    AddMetadataValue(SB_PROPERTY_GENRE,           ConvertCharset(pTag->genre(), aCharset));
    AddMetadataValue(SB_PROPERTY_PRODUCERNAME,    ConvertCharset(pTag->producer(), aCharset));
    AddMetadataValue(SB_PROPERTY_COMPOSERNAME,    ConvertCharset(pTag->composer(), aCharset));
    AddMetadataValue(SB_PROPERTY_CONDUCTORNAME,   ConvertCharset(pTag->conductor(), aCharset));
    AddMetadataValue(SB_PROPERTY_LYRICISTNAME,    ConvertCharset(pTag->lyricist(), aCharset));
    AddMetadataValue(SB_PROPERTY_RECORDLABELNAME, ConvertCharset(pTag->recordLabel(), aCharset));
    AddMetadataValue(SB_PROPERTY_RATING,          ConvertCharset(pTag->rating(), aCharset));
    AddMetadataValue(SB_PROPERTY_LANGUAGE,        ConvertCharset(pTag->language(), aCharset));
    AddMetadataValue(SB_PROPERTY_KEY,             ConvertCharset(pTag->key(), aCharset));
    AddMetadataValue(SB_PROPERTY_COPYRIGHT,       ConvertCharset(pTag->license(), aCharset));
    AddMetadataValue(SB_PROPERTY_COPYRIGHTURL,    ConvertCharset(pTag->licenseUrl(), aCharset));
    AddMetadataValue(SB_PROPERTY_YEAR,            pTag->year());
    AddMetadataValue(SB_PROPERTY_TRACKNUMBER,     pTag->track());
    AddMetadataValue(SB_PROPERTY_TOTALTRACKS,     pTag->totalTracks());
    AddMetadataValue(SB_PROPERTY_DISCNUMBER,      pTag->disc());
    AddMetadataValue(SB_PROPERTY_TOTALDISCS,      pTag->totalDiscs());
    AddMetadataValue(SB_PROPERTY_BPM,             pTag->bpm());
    // todo: compilation!
  }
  
  pAudioProperties = pTagFile->audioProperties();
  if (pAudioProperties)
  {
      AddMetadataValue(SB_PROPERTY_BITRATE, pAudioProperties->bitrate());
      AddMetadataValue(SB_PROPERTY_SAMPLERATE, pAudioProperties->sampleRate());
      AddMetadataValue(SB_PROPERTY_DURATION, pAudioProperties->length() * 1000000);
  }
  
  return true; // file was valid
}


/*
 * GuessCharset
 *
 *   --> pTag                   The tags to look at
 *
 *   <--                        The best guess at the encoding the tags are in
 *
 *   This function looks at a a set of tags and tries to guess what charset
 *   (encoding) the tags are in.  Returns empty string to mean don't convert
 *   (valid Unicode), and the literal "CP_ACP" if on Windows and best guess is
 *   whatever the system locale is.
 */

void sbMetadataHandlerTaglib::GuessCharset(
    TagLib::Tag*                pTag,
    nsACString&                 _retval)
{
    nsresult rv;

    if (!pTag) {
        // no tag to read
        _retval.Truncate();
        return;
    }

    // first, build a string consisting of some tags
    TagLib::String tagString;
    tagString += pTag->album();
    tagString += pTag->artist();
    tagString += pTag->title();

    // comment and genre can end up confusing the detection; ignore them
    //tagString += pTag->comment();
    //tagString += pTag->genre();

    // first, figure out if this was from unicode - we do this by scanning the
    // string for things that had more than 8 bits.

    // XXXben We can't use the TagLib::String iterators here because TagLib was
    //        not compiled with -fshort_wchar whereas this component (and
    //        mozilla) are. Iterate manually instead.
    std::string data = tagString.toCString(true);
    NS_ConvertUTF8toUTF16 expandedData(data.c_str());

    #if PR_LOGGING && DEBUG
    {
      nsCOMPtr<nsINetUtil> netUtil =
        do_GetService("@mozilla.org/network/util;1", &rv);
      NS_ENSURE_SUCCESS(rv, /* void */);
      
      nsCString escaped;
      rv = netUtil->EscapeString(NS_ConvertUTF16toUTF8(expandedData),
                                 nsINetUtil::ESCAPE_ALL,
                                 escaped);
      NS_ENSURE_SUCCESS(rv, /* void */);
      
      LOG(("sbMetadataHandlerTaglib::GuessCharset: guessing with data %s",
           escaped.BeginReading()));
    }
    #endif /* PR_LOGGING && DEBUG */

    const PRUnichar *begin, *end;
    expandedData.BeginReading(&begin, &end);

    PRBool is7Bit = PR_TRUE;
    while (begin < end) {
        PRUnichar character = *begin++;
        if (character & ~0xFF) {
            _retval.Truncate();
            return;
        }
        if (character & 0x80) {
            is7Bit = PR_FALSE;
        }
    }

    if (is7Bit) {
        _retval.AssignLiteral("us-ascii");
        return;
    }

    // XXXben The code below is going to run *slowly*, but hopefully we already
    //        exited for UTF16 and ASCII.

    // see if it's valid utf8; if yes, assume it _is_ indeed utf8
    nsCString raw(tagString.toCString());
    if (IsLikelyUTF8(raw)) {
        // Drop taglib lock around proxied service usage; otherwise we can
        // deadlock with the main thread trying to grab the taglib lock for
        // another file
        nsAutoUnlock lock(sTaglibLock);
        nsCOMPtr<nsIUTF8ConverterService> utf8Service;
        mProxiedServices->GetUtf8ConverterService(getter_AddRefs(utf8Service));
        if (utf8Service) {
            nsCString dataUTF8;
            rv = utf8Service->ConvertStringToUTF8(raw,
                                                  "utf-8",
                                                  PR_FALSE,
                                                  dataUTF8);
            if (NS_SUCCEEDED(rv)) {
                // this was utf8
                _retval.AssignLiteral("utf-8");
                return;
            }
        }
    }

    // the metadata is in some 8-bit encoding; try to guess
    nsCOMPtr<nsICharsetDetector> detector = do_CreateInstance(
        NS_CHARSET_DETECTOR_CONTRACTID_BASE "universal_charset_detector");
    rv = RunCharsetDetector(detector, tagString);
    if (NS_SUCCEEDED(rv)) {
        if (eSureAnswer == mLastConfidence || eBestAnswer == mLastConfidence) {
            _retval.Assign(mLastCharset);
            LOG(("sbMetadataHandlerTaglib::GuessCharset: detected answer %s",
                 _retval.BeginReading()));

            // Bug 8394 - The universal charset detector likes to treat cp1251
            // as MacUkranian, so in this case run the specific Ukranian 
            // detector since it does a better job.
            if (mLastCharset.EqualsLiteral("x-mac-cyrillic")) {
                detector = do_CreateInstance(
                    NS_CHARSET_DETECTOR_CONTRACTID_BASE "ukprob");
                rv = RunCharsetDetector(detector, tagString);
                if (NS_SUCCEEDED(rv)) {
                    if (eSureAnswer == mLastConfidence || 
                        eBestAnswer == mLastConfidence) 
                    {
                        _retval.Assign(mLastCharset);
                    }
                }
            }
            return;
        }
    }

#if XP_WIN
    // we have no idea what charset this is, but we know it's bad.
    // for Windows only, assume CP_ACP

    // make the call fail if it's not valid CP_ACP
    int size = MultiByteToWideChar( CP_ACP,
                                    MB_ERR_INVALID_CHARS,
                                    data.c_str(),
                                    data.length(),
                                    nsnull,
                                    0 );
    if (size) {
        // okay, so CP_ACP is usable
        _retval.AssignLiteral("CP_ACP");
        return;
    }
#endif

    // we truely know nothing
    _retval.Truncate();
}

/*
 * RunCharsetDetector
 *
 *   Run the given nsICharsetDetector.  Results will be available in 
 *   mLastConfidence and mLastCharset.
 */

nsresult sbMetadataHandlerTaglib::RunCharsetDetector(
    nsICharsetDetector*             aDetector,
    TagLib::String&                 aContent)
{
    NS_ENSURE_ARG_POINTER(aDetector);
    nsresult rv = NS_OK;
    
    mLastConfidence = eNoAnswerYet;

    nsCOMPtr<nsICharsetDetectionObserver> observer =
        do_QueryInterface( NS_ISUPPORTS_CAST(nsICharsetDetectionObserver*, this) );
    NS_ASSERTION(observer, "Um! We're supposed to be implementing this...");

    rv = aDetector->Init(observer);
    if (NS_SUCCEEDED(rv)) {
        PRBool isDone;
        // artificially inflate the buffer by repeating it a lot; this does
        // in fact help with the detection
        const PRUint32 chunkSize = aContent.size();
        std::string raw = aContent.toCString();
        PRUint32 currentSize = 0;
        while (currentSize < GUESS_CHARSET_MIN_CHAR_COUNT) {
            rv = aDetector->DoIt(raw.c_str(), chunkSize, &isDone);
            if (NS_FAILED(rv) || isDone) {
                break;
            }
            currentSize += chunkSize;
        }
        if (NS_SUCCEEDED(rv)) {
            rv = aDetector->Done();
        }
    }
    return rv;
}

/* nsICharsetDetectionObserver */
NS_IMETHODIMP sbMetadataHandlerTaglib::Notify(
    const char                  *aCharset,
    nsDetectionConfident        aConf)
{
    mLastCharset.AssignLiteral(aCharset);
    mLastConfidence = aConf;
    return NS_OK;
}

TagLib::String sbMetadataHandlerTaglib::ConvertCharset(
    TagLib::String              aString,
    const char                  *aCharset)
{
    // if null, return an empty (non-null) string instead
    if (aString.isNull()) {
        return TagLib::String();
    }

    // If UTF16 or ASCII, or we have no idea, 
    // just leave the string as-is
    if (!aCharset || !*aCharset ||
        !aString.shouldGuessCharacterSet() ||
        !strcmp("utf-8", aCharset) ||
        !strcmp("us-ascii", aCharset))
        
    {
        LOG(("sbMetadataHandlerTaglib::ConvertCharset: not converting to \"%s\" (guess? %i)",
             aCharset ? aCharset : "(null)",
             aString.shouldGuessCharacterSet()
             ));
        return aString;
    }

    std::string data = aString.toCString(false);
#if XP_WIN
    if (strcmp("CP_ACP", aCharset) == 0) {
        // convert to CP_ACP
        int size = ::MultiByteToWideChar( CP_ACP,
                                          MB_ERR_INVALID_CHARS,
                                          data.c_str(),
                                          data.length(),
                                          nsnull,
                                          0 );
        if (size) {
            // convert to CP_ACP
            PRUnichar *wstr = reinterpret_cast< PRUnichar * >( NS_Alloc( (size + 1) * sizeof( PRUnichar ) ) );
            if (wstr) {
                int read = MultiByteToWideChar( CP_ACP,
                                                MB_ERR_INVALID_CHARS,
                                                data.c_str(),
                                                data.length(),
                                                wstr,
                                                size );
                NS_ASSERTION(size == read, "Win32 Current Codepage conversion failed.");

                wstr[size] = '\0';

                TagLib::String strValue(wstr);
                NS_Free(wstr);
                return strValue;
            }
        }
    }
#endif

    // convert via Mozilla
    // TODO XXX This call takes 1/3 of our scan time right now!
    nsCString raw(data.c_str(), data.length());

    {
      // Release the lock while we're using proxied services to avoid
      // deadlocking with the main thread trying to grab the taglib lock
      nsAutoUnlock lock(sTaglibLock);

      nsCOMPtr<nsIUTF8ConverterService> utf8Service;
      mProxiedServices->GetUtf8ConverterService(getter_AddRefs(utf8Service));
      if (utf8Service) {
          nsCString converted;
          nsresult rv = utf8Service->ConvertStringToUTF8(
              raw, aCharset, PR_FALSE, converted);
          if (NS_SUCCEEDED(rv))
              return TagLib::String(converted.BeginReading(), TagLib::String::UTF8);
      }
    }

    // failed to convert :(
    return aString;
}


/*
 * ReadFLACFile
 *   <--                        True if file has valid FLAC metadata.
 *
 *   This function reads metadata from the FLAC file with the file path
 * specified by mMetadataPath.
 */

PRBool sbMetadataHandlerTaglib::ReadFLACFile()
{
    nsAutoPtr<TagLib::FLAC::File>   pTagFile;
    PRBool                          isValid = PR_TRUE;
    nsresult                        result = NS_OK;

    /* Open and read the metadata file. */
    pTagFile = new TagLib::FLAC::File();
    if (!pTagFile)
        result = NS_ERROR_OUT_OF_MEMORY;
    if (NS_SUCCEEDED(result))
        result = OpenTagFile(pTagFile);
    if (NS_SUCCEEDED(result))
        pTagFile->read();
    if (NS_SUCCEEDED(result))
        result = CheckChannelRestart();

    /* Read the base file metadata. */
    if (NS_SUCCEEDED(result) && isValid)
        isValid = ReadFile(pTagFile);

    /* Read the Xiph comment metadata. */
    if (NS_SUCCEEDED(result) && isValid)
        ReadXiphTags(pTagFile->xiphComment());

    /* File is invalid on any error. */
    if (NS_FAILED(result))
        isValid = PR_FALSE;

    return (isValid);
}


/*
 * ReadMPCFile
 *   <--                        True if file has valid MPC metadata.
 *
 *   This function reads metadata from the MPC file with the file path specified
 * by mMetadataPath.
 */

PRBool sbMetadataHandlerTaglib::ReadMPCFile()
{
    nsAutoPtr<TagLib::MPC::File>    pTagFile;
    PRBool                          isValid = PR_TRUE;
    nsresult                        result = NS_OK;

    pTagFile = new TagLib::MPC::File();
    if (!pTagFile)
        result = NS_ERROR_OUT_OF_MEMORY;
    if (NS_SUCCEEDED(result))
        result = OpenTagFile(pTagFile);
    if (NS_SUCCEEDED(result))
        pTagFile->read();
    if (NS_SUCCEEDED(result))
        result = CheckChannelRestart();

    /* Read the base file metadata. */
    if (NS_SUCCEEDED(result) && isValid)
        isValid = ReadFile(pTagFile);

    /* Read the APE metadata */
    if (NS_SUCCEEDED(result) && isValid)
        ReadAPETags(pTagFile->APETag());

    /* File is invalid on any error. */
    if (NS_FAILED(result))
        isValid = PR_FALSE;

    return (isValid);
}


/*
 * ReadMPEGFile
 *   <--                        True if file has valid MPEG metadata.
 *
 *   This function reads metadata from the MPEG file with the file path
 * specified by mMetadataPath.
 */

PRBool sbMetadataHandlerTaglib::ReadMPEGFile()
{
    nsAutoPtr<TagLib::MPEG::File>   pTagFile;
    PRBool                          isValid = PR_TRUE;
    nsresult                        result = NS_OK;
    
    pTagFile = new TagLib::MPEG::File();
    if (!pTagFile)
        result = NS_ERROR_OUT_OF_MEMORY;
    if (NS_SUCCEEDED(result))
        result = OpenTagFile(pTagFile);
    if (NS_SUCCEEDED(result))
        pTagFile->read();
    if (NS_SUCCEEDED(result))
        result = CheckChannelRestart();

    /* Guess the charset */
    nsCString charset;
    if (NS_SUCCEEDED(result)) {
        GuessCharset(pTagFile->tag(), charset);
        LOG(("sbMetadataHandlerTaglib::ReadMPEGFile: Guessed charset: %s",
             charset.BeginReading()));
    }
    
    /* Read the base file metadata. */
    if (NS_SUCCEEDED(result) && isValid)
        isValid = ReadFile(pTagFile, charset.BeginReading());

    /* Read the ID3v2 metadata. */
    if (NS_SUCCEEDED(result) && isValid)
        ReadID3v2Tags(pTagFile->ID3v2Tag(), charset.BeginReading());

    /* Read the APE metadata */
    if (NS_SUCCEEDED(result) && isValid)
        ReadAPETags(pTagFile->APETag());

    /* File is invalid on any error. */
    if (NS_FAILED(result))
        isValid = PR_FALSE;

    return (isValid);
}


/*
 * ReadMP4File
 *   <--                        True if file has valid MP4 metadata.
 *
 *   This function reads metadata from the MP4 file with the file path specified
 * by mMetadataPath.
 */

PRBool sbMetadataHandlerTaglib::ReadMP4File()
{
    nsAutoPtr<TagLib::MP4::File>    pTagFile;
    PRBool                          isValid = PR_TRUE;
    nsresult                        result = NS_OK;
    
    pTagFile = new TagLib::MP4::File();
    if (!pTagFile)
        result = NS_ERROR_OUT_OF_MEMORY;
    if (NS_SUCCEEDED(result))
        result = OpenTagFile(pTagFile);
    if (NS_SUCCEEDED(result))
        pTagFile->read();
    if (NS_SUCCEEDED(result))
        result = CheckChannelRestart();

    /* Read the base file metadata. */
    if (NS_SUCCEEDED(result))
        isValid = ReadFile(pTagFile);

    /* File is invalid on any error. */
    if (NS_FAILED(result))
        isValid = PR_FALSE;

    return (isValid);
}


/*
 * ReadOGGFile
 *   <--                        True if file has valid OGG metadata.
 *
 *   This function reads metadata from the OGG file with the file path specified
 * by mMetadataPath.
 */

PRBool sbMetadataHandlerTaglib::ReadOGGFile()
{
    nsAutoPtr<TagLib::Vorbis::File> pTagFile;
    PRBool                          isValid = PR_TRUE;
    nsresult                        result = NS_OK;
    
    pTagFile = new TagLib::Vorbis::File();
    if (!pTagFile)
        result = NS_ERROR_OUT_OF_MEMORY;
    if (NS_SUCCEEDED(result))
        result = OpenTagFile(pTagFile);
    if (NS_SUCCEEDED(result))
        pTagFile->read();
    if (NS_SUCCEEDED(result))
        result = CheckChannelRestart();

    /* Read the base file metadata. */
    if (NS_SUCCEEDED(result))
        isValid = ReadFile(pTagFile);

    /* Read the Xiph metadata. */
    if (NS_SUCCEEDED(result) && isValid)
        ReadXiphTags(pTagFile->tag());

    /* File is invalid on any error. */
    if (NS_FAILED(result))
        isValid = PR_FALSE;

    return (isValid);
}


/*
 * AddMetadataValue
 *
 *   --> name                   Metadata name.
 *   --> value                  Metadata value.
 *
 *   This function adds the metadata value specified by value with the name
 * specified by name to the set of metadata values.
 */

nsresult sbMetadataHandlerTaglib::AddMetadataValue(
    const char                  *name,
    TagLib::String              value)
{
    nsresult                    result = NS_OK;

    /* Do nothing if no metadata value available. */
    if (value.isNull())
        return (result);

    /* Add the metadata value. */
    result = mpMetadataPropertyArray->AppendProperty
                        (NS_ConvertASCIItoUTF16(name),
                         NS_ConvertUTF8toUTF16(value.toCString(true)));

    return (result);
}


/*
 * AddMetadataValue
 *
 *   --> name                   Metadata name.
 *   --> value                  Metadata value.
 *
 *   This function adds the metadata value specified by value with the name
 * specified by name to the set of metadata values.
 */

nsresult sbMetadataHandlerTaglib::AddMetadataValue(
    const char                  *name,
    TagLib::uint                value)
{
    nsAutoString                valueString;
    nsresult                    result = NS_OK;

    //  Zero indicates no value
    if (value == 0) {
      return result;
    }

    /* Convert the integer value into a string. */    
    valueString.AppendInt(value);

    /* Add the metadata value. */
    result = mpMetadataPropertyArray->AppendProperty
                                     (NS_ConvertASCIItoUTF16(name),
                                      valueString);

    return (result);
}
