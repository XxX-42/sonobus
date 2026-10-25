// SPDX-License-Identifier: GPLv3-or-later WITH Appstore-exception
// Copyright (C) 2020 Jesse Chappell

/*
  ==============================================================================

   This file is part of the JUCE library.
   Copyright (c) 2017 - ROLI Ltd.

   JUCE is an open source library subject to commercial or open-source
   licensing.

   By using JUCE, you agree to the terms of both the JUCE 5 End-User License
   Agreement and JUCE 5 Privacy Policy (both updated and effective as of the
   27th April 2017).

   End User License Agreement: www.juce.com/juce-5-licence
   Privacy Policy: www.juce.com/juce-5-privacy-policy

   Or: You may also use this code under the terms of the GPL v3 (see
   www.gnu.org/licenses).

   JUCE IS PROVIDED "AS IS" WITHOUT ANY WARRANTY, AND ALL WARRANTIES, WHETHER
   EXPRESSED OR IMPLIED, INCLUDING MERCHANTABILITY AND FITNESS FOR PURPOSE, ARE
   DISCLAIMED.

  ==============================================================================
*/

// needed for crappy windows
#define NOMINMAX


#include "JuceHeader.h"

#include "juce_core/system/juce_TargetPlatform.h"
#include "juce_audio_plugin_client/detail/juce_CheckSettingMacros.h"

#if !JUCE_LINUX
#include "juce_audio_plugin_client/detail/juce_IncludeSystemHeaders.h"
#include "juce_audio_plugin_client/detail/juce_IncludeModuleHeaders.h"
#include "juce_gui_basics/native/juce_WindowsHooks_windows.h"
#endif

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <cmath>


// You can set this flag in your build if you need to specify a different
// standalone JUCEApplication class for your app to use. If you don't
// set it then by default we'll just create a simple one as below.
//#if ! JUCE_USE_CUSTOM_PLUGIN_STANDALONE_APP

extern juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

#include "SonoStandaloneFilterWindow.h"
#include "SonoLookAndFeel.h"

#include "SonobusPluginEditor.h"

#if JUCE_ANDROID
#include "android/SonoBusActivity.h"

#if JUCE_USE_ANDROID_OPENSLES || JUCE_USE_ANDROID_OBOE
  #include "juce_audio_devices/native/juce_HighPerformanceAudioHelpers_android.h"
#endif

#endif

namespace juce
{

//==============================================================================
class SonobusStandaloneFilterApp  : public JUCEApplication, public Timer
{
public:
    SonobusStandaloneFilterApp()
    {
        DBG("SONOBUS_HEADLESS_TCC_REBUILD_MARKER_C");
        PluginHostType::jucePlugInClientCurrentWrapperType = AudioProcessor::wrapperType_Standalone;

        PropertiesFile::Options options;

        options.applicationName     = getApplicationName();
        options.filenameSuffix      = ".settings";
        options.osxLibrarySubFolder = "Application Support/SonoBus";
       #if JUCE_LINUX
        options.folderName          = "~/.config/sonobus";
       #else
        options.folderName          = "";
       #endif

        appProperties.setStorageParameters (options);

#if JUCE_LINUX
        // we moved linux settings location in 1.3.19, one time change
        File oldsettings("~/.config/SonoBus.settings");
        if (oldsettings.exists()) {
            File newsettings = options.getDefaultFile();
            if (!newsettings.getParentDirectory().exists()) {
                newsettings.getParentDirectory().createDirectory();
                oldsettings.moveFileTo(newsettings);
            }
        }
#endif
    }

    ~SonobusStandaloneFilterApp()
    {
#if JUCE_ANDROID && JUCE_OPENGL
        detachGL();
#endif
    }

    const String getApplicationName() override              { return JucePlugin_Name; }
    const String getApplicationVersion() override           { return JucePlugin_VersionString; }
    bool moreThanOneInstanceAllowed() override              { return true; }

    SonoLookAndFeel  sonoLNF;

    AooServerConnectionInfo cmdlineConnInfo;

    bool copyInfo = false;
    bool doInitialConnect = false;
    bool doImmediateQuit = false;
    bool doHeadless = false;
    bool doDirectPeerConnect = false;
    String directPeerHost;
    int directPeerPort = 0;
    String loadSetupFilename;
    String cmdlineArgUrl;
    StringArray headlessAppliedPeerAddresses;

    virtual StandalonePluginHolder* createHeadlessPlugin ()
    {
#ifdef JucePlugin_PreferredChannelConfigurations
        StandalonePluginHolder::PluginInOuts channels[] = { JucePlugin_PreferredChannelConfigurations };
#endif

        AudioDeviceManager::AudioDeviceSetup setupOptions;
        setupOptions.sampleRate = 48000;
#if JUCE_MAC
        setupOptions.bufferSize = 128;
#elif JUCE_ANDROID
        setupOptions.bufferSize = 192;
        setupOptions.sampleRate = AndroidHighPerformanceAudioHelpers::getNativeSampleRate();
#else
        setupOptions.bufferSize = 256;
#endif

        File settingsFile = appProperties.getStorageParameters().getDefaultFile();
        File crashSentinelFile = settingsFile.getSiblingFile("SENTINEL");
        if (crashSentinelFile.existsAsFile()) {
            DBG("CRASH SENTINEL STILL EXISTS, moving old settings away!");
            File oldfile = settingsFile.getSiblingFile(String("POSSIBLY_BAD_") + Time::getCurrentTime().formatted("%Y-%m-%d_%H.%M.%S"));
            settingsFile.moveFileTo(oldfile);
            Timer::callAfterDelay (800, []()
                                   {
                AlertWindow::showMessageBoxAsync (AlertWindow::WarningIcon,
                                                  TRANS("Crashed Last Time"),
                                                  TRANS("Looks like you crashed on launch last time, restoring default settings!"));
            });
        }
        else {
            crashSentinelFile.create();
        }

        String prefDevname;

        auto plugh = new StandalonePluginHolder (appProperties.getUserSettings(), false,
                                                 prefDevname, &setupOptions
#ifdef JucePlugin_PreferredChannelConfigurations
                                                 , juce::Array<StandalonePluginHolder::PluginInOuts> (channels, juce::numElementsInArray (channels))
#else
                                                 , {}
#endif
                                                 , false);


        // if we got here, we didn't crash on initialization!
        crashSentinelFile.deleteFile();

        return plugh;
    }

    virtual StandaloneFilterWindow* createWindow()
    {
       #ifdef JucePlugin_PreferredChannelConfigurations
        StandalonePluginHolder::PluginInOuts channels[] = { JucePlugin_PreferredChannelConfigurations };
       #endif

        AudioDeviceManager::AudioDeviceSetup setupOptions;
        setupOptions.sampleRate = 48000;
#if JUCE_MAC
        setupOptions.bufferSize = 128;
#elif JUCE_ANDROID
        setupOptions.bufferSize = 192;
        setupOptions.sampleRate = AndroidHighPerformanceAudioHelpers::getNativeSampleRate();
#else
        setupOptions.bufferSize = 256;
#endif

        File settingsFile = appProperties.getStorageParameters().getDefaultFile();
        File crashSentinelFile = settingsFile.getSiblingFile("SENTINEL");
        if (crashSentinelFile.existsAsFile()) {
            DBG("CRASH SENTINEL STILL EXISTS, moving old settings away!");
            File oldfile = settingsFile.getSiblingFile(String("POSSIBLY_BAD_") + Time::getCurrentTime().formatted("%Y-%m-%d_%H.%M.%S"));
            settingsFile.moveFileTo(oldfile);
            Timer::callAfterDelay (800, []()
            {
                AlertWindow::showMessageBoxAsync (AlertWindow::WarningIcon,
                                                  TRANS("Crashed Last Time"),
                                                  TRANS("Looks like you crashed on launch last time, restoring default settings!"));
            });
        }
        else {
            crashSentinelFile.create();
        }

        LookAndFeel::setDefaultLookAndFeel(&sonoLNF);

        auto wind = new StandaloneFilterWindow (getApplicationName(),
                                           LookAndFeel::getDefaultLookAndFeel().findColour (ResizableWindow::backgroundColourId),
                                           appProperties.getUserSettings(),
                                           false, {}, &setupOptions
                                          #ifdef JucePlugin_PreferredChannelConfigurations
                                           , juce::Array<StandalonePluginHolder::PluginInOuts> (channels, juce::numElementsInArray (channels))
                                          #else
                                           , {}
                                          #endif
                                          //#if JUCE_DONT_AUTO_OPEN_MIDI_DEVICES_ON_MOBILE
                                           , false
                                          //#endif
                                           );

        // if we got here, we didn't crash on initialization!
        crashSentinelFile.deleteFile();

        return wind;
    }

    void setupDefaultConnInfo() {
        String username;// = processor.getCurrentUsername();

        if (username.isEmpty()) {
#if JUCE_IOS
            //String username = SystemStats::getFullUserName(); //SystemStats::getLogonName();
            username = SystemStats::getComputerName(); //SystemStats::getLogonName();
#else
            username = SystemStats::getFullUserName(); //SystemStats::getLogonName();
            //if (username.length() > 0) username = username.replaceSection(0, 1, username.substring(0, 1).toUpperCase());
#endif
        }
        if (username.isEmpty()) { // fallback
            username = SystemStats::getComputerName();
        }

        cmdlineConnInfo.userName = username.trim();

        cmdlineConnInfo.serverHost = DEFAULT_SERVER_HOST;
        cmdlineConnInfo.serverPort = DEFAULT_SERVER_PORT;
    }

    static void printCommandDescription (const ArgumentList& args, const ConsoleApplication::Command& command,
                                         int descriptionIndent)
    {
        auto carg = command.argumentDescription;

        if (carg.length() > descriptionIndent)
            std::cout << "  " << carg << std::endl << String().paddedRight (' ', descriptionIndent);
        else
            std::cout << "  " << carg.paddedRight (' ', descriptionIndent);

        std::cout << command.shortDescription << std::endl;

        if (command.longDescription.isNotEmpty()) {
            std::cout << "     " << command.longDescription << std::endl;
        }
    }

    void printCommandList (ConsoleApplication & capp, const ArgumentList& args) const
    {
        int descriptionIndent = 4;
        auto commands = capp.getCommands();

        for (auto& c : commands)
            descriptionIndent = std::max (descriptionIndent, c.argumentDescription.length());

        descriptionIndent = std::min (descriptionIndent + 2, 40);

        for (auto& c : commands)
            printCommandDescription (args, c, descriptionIndent);

        std::cout << std::endl;
    }

    void handleCommandLine()
    {
        ConsoleApplication app;

        const String versionSpec("-v|--version");
        const String helpSpec("-h|--help");

        const String serverSpec("-c|--connectionserver");
        const String serverSpecDesc("-c|--connectionserver <address[:port]>");

        const String groupSpec("-g|--group");
        const String groupSpecDesc("-g|--group <groupname>");

        const String groupPassSpec("-p|--group-password");
        const String groupPassSpecDesc("-p|--group-password <password>");

        const String userNameSpec("-n|--username");
        const String userNameSpecDesc("-n|--username <username>");

        const String directPeerSpec("-d|--direct-peer");
        const String directPeerSpecDesc("-d|--direct-peer <address:udp-port>");

        const String headlessSpec("-q|--headless");
        const String headlessSpecDesc("-q|--headless");

        const String loadSetupSpec("-l|--load-setup");
        const String loadSetupSpecDesc("-l|--load-setup <setup-filename>");

        

        app.addCommand ({ helpSpec, helpSpec, TRANS("Prints the list of commands"), {}, nullptr });
        app.addCommand ({ versionSpec, versionSpec, TRANS("Prints the current version number only"), {}, nullptr });


        auto params = getCommandLineParameterArray();
        ArgumentList arglist(getApplicationName(), params);

        app.addCommand ({ groupSpec, groupSpecDesc,
            TRANS("Specify the group name to immediately connect to upon launch"), {},
            nullptr
        });

        app.addCommand ({ userNameSpec, userNameSpecDesc,
            TRANS("Specify the displayed username for yourself when connecting to a group"), {},
            nullptr
        });

        app.addCommand ({ directPeerSpec, directPeerSpecDesc,
            TRANS("Headless direct peer invite by IP and UDP port; group name and password are not used"), {},
            nullptr
        });

        app.addCommand ({ groupPassSpec, groupPassSpecDesc,
            TRANS("Specify the password to use for the group name to connect to (optional)"), {},
            nullptr
        });

        app.addCommand ({ serverSpec, serverSpecDesc,
            TRANS("Specify connection server to use when connecting to a group (optional)"), {},
            nullptr
        });

        app.addCommand ({ loadSetupSpec, loadSetupSpecDesc,
            TRANS("Specify the filename of a setup file to load."),
            TRANS("The setup file can be created using the Save Setup feature from the full application, and includes any device selection, input mixer setup, and all other options. If you don't specify a full or relative pathname it will look for a preset file by that name in the last used setup folder."),
            nullptr
        });

        app.addCommand ({ headlessSpec, headlessSpecDesc,
            TRANS("If specified, no GUI will be used and the application will be run headless."),
            TRANS("You'll need to use other command-line options to connect to a group... eventually there will be an OSC remote control interface."),
            nullptr
        });



        if (arglist.removeOptionIfFound(versionSpec)) {
            std::cout << getApplicationName() << TRANS(" version ") << getApplicationVersion() << std::endl;
            doImmediateQuit = true;
        }

        if (arglist.containsOption("-h|--help")) {
            std::cout << TRANS("Usage: ") << getApplicationName() << " [options...]  [connect_URL]" << std::endl;

            printCommandList(app, arglist);
            doImmediateQuit = true;
        }

        if (doImmediateQuit) {
            return;
        }

        setupDefaultConnInfo();

        auto connserv = arglist.removeValueForOption(serverSpec);
        if (connserv.isNotEmpty()) {
            cmdlineConnInfo.serverHost =  connserv.upToFirstOccurrenceOf(":", false, true);
            String portpart = connserv.fromFirstOccurrenceOf(":", false, false);
            int port = portpart.getIntValue();
            if (port > 0) {
                cmdlineConnInfo.serverPort = port;
            } else {
                cmdlineConnInfo.serverPort = DEFAULT_SERVER_PORT;
            }
            copyInfo = true;
        }

        auto groupname = arglist.removeValueForOption(groupSpec);
        if (groupname.isNotEmpty()) {
            cmdlineConnInfo.groupName = groupname.trim();
            doInitialConnect = true;
            copyInfo = true;
        }

        auto grouppass = arglist.removeValueForOption(groupPassSpec);
        if (grouppass.isNotEmpty()) {
            cmdlineConnInfo.groupPassword = grouppass;
            copyInfo = true;
        }

        auto username = arglist.removeValueForOption(userNameSpec);
        if (username.isNotEmpty()) {
            cmdlineConnInfo.userName = username.trim();
            copyInfo = true;
        }

        auto directPeer = arglist.removeValueForOption(directPeerSpec).trim();
        if (directPeer.isNotEmpty()) {
            const int separator = directPeer.lastIndexOfChar(':');
            if (separator <= 0 || separator >= directPeer.length() - 1) {
                std::cerr << "Error: --direct-peer requires an address and UDP port, for example 192.168.1.20:12000" << std::endl;
                doImmediateQuit = true;
            } else {
                directPeerHost = directPeer.substring(0, separator).trim();
                directPeerPort = directPeer.substring(separator + 1).getIntValue();
                if (directPeerHost.isEmpty() || directPeerPort < 1 || directPeerPort > 65535) {
                    std::cerr << "Error: invalid --direct-peer address or UDP port" << std::endl;
                    doImmediateQuit = true;
                } else if (doInitialConnect) {
                    std::cerr << "Error: use either a group connection or --direct-peer, not both" << std::endl;
                    doImmediateQuit = true;
                } else {
                    doDirectPeerConnect = true;
                    doHeadless = true;
                }
            }
        }

        auto setupfile = arglist.removeValueForOption(loadSetupSpec);
        if (setupfile.isNotEmpty()) {
            loadSetupFilename = setupfile;
        }


        if (arglist.removeOptionIfFound(headlessSpec)) {

            doHeadless = true;

            if (!doInitialConnect && !doDirectPeerConnect) {
                std::cout << TRANS("Error: you need to specify a group to connect to for headless operation right now... eventually there will be an OSC interface.") << std::endl;
                doImmediateQuit = true;
            }
        }

        // what args remain? assume it's a URL
        if (arglist.arguments.size() > 0) {
            cmdlineArgUrl = arglist.arguments.getLast().text;
        }

    }

    //==============================================================================
    void initialise (const String&) override
    {

        handleCommandLine();

        if (doImmediateQuit) {
            quit();
            return;
        };


        if (!doHeadless) {
            mainWindow.reset (createWindow());

#if JUCE_STANDALONE_FILTER_WINDOW_USE_KIOSK_MODE
            Desktop::getInstance().setKioskModeComponent (mainWindow.get(), false);
#endif

            mainWindow->setVisible (true);

            Desktop::getInstance().setScreenSaverEnabled(false);


            if (auto * sonoproc = dynamic_cast<SonobusAudioProcessor*>(mainWindow->pluginHolder->processor.get())) {
                if (sonoproc->hasEditor()) {
                    if (auto * sonoeditor = dynamic_cast<SonobusAudioProcessorEditor*>(sonoproc->createEditorIfNeeded())) {
                        sonoeditor->saveSettingsIfNeeded = [this]() {
                            mainWindow->pluginHolder->savePluginState();
                            mainWindow->pluginHolder->saveAudioDeviceState();
                            appProperties.saveIfNeeded();
                        };

                        // apply command line connection stuff

                        if (doInitialConnect) {
                            DBG("CONNECTING INITIAL");
                            sonoeditor->connectWithInfo(cmdlineConnInfo, false, false);
                        } else if (copyInfo) {
                            // only copy info
                            DBG("COPYING INITIAL");
                            sonoeditor->connectWithInfo(cmdlineConnInfo, false, true);
                        }
                        else if (cmdlineArgUrl.isNotEmpty()) {
#if JUCE_LINUX
                            // Linux only, as this is handled through other means on other platforms
                            // handle the last arg as a connect URL
                            sonoeditor->handleURL(cmdlineArgUrl);
#endif
                        }

                        if (loadSetupFilename.isNotEmpty()) {
                            File setupfile = File::getCurrentWorkingDirectory().getChildFile(loadSetupFilename);
                            if (!setupfile.exists()) {
                                // try the default location
                                String recentsfolder = mainWindow->pluginHolder->getLastRecentsFolder();
                                if (recentsfolder.isNotEmpty()) {
                                    setupfile = File(recentsfolder).getChildFile(setupfile.getFileName());
                                }
                            }
                            if (setupfile.exists()) {
                                Thread::sleep(200); // just in case
                                sonoeditor->loadSettingsFromFile(setupfile);
                            }
                            else {
                                std::cerr << "Settings file does not exist: " << loadSetupFilename << std::endl;
                            }
                        }
                    }
                }
            }

#if JUCE_ANDROID && JUCE_OPENGL
            attachGL();
#endif


#if JUCE_ANDROID
            startTimer(500);
#endif
        }
        else {
            // headless mode

            pluginHolder.reset (createHeadlessPlugin());

            if (auto * sonoproc = dynamic_cast<SonobusAudioProcessor*>(pluginHolder->processor.get())) {

                // apply command line connection stuff

                if (loadSetupFilename.isNotEmpty()) {
                    File setupfile = File::getCurrentWorkingDirectory().getChildFile(loadSetupFilename);
                    if (!setupfile.exists()) {
                        // try the default location
                        String recentsfolder = pluginHolder->getLastRecentsFolder();
                        if (recentsfolder.isNotEmpty()) {
                            setupfile = File(recentsfolder).getChildFile(setupfile.getFileName());
                        }
                    }
                    if (setupfile.exists()) {
                        Thread::sleep(200); // just in case

                        if (loadSettingsFromFile(setupfile)) {
                            std::cerr << "Loaded Settings file: " << setupfile.getFullPathName() << std::endl;
                        }
                        else {
                            std::cerr << "Error loading settings file: " << setupfile.getFullPathName() << std::endl;
                        }
                    }
                    else {
                        std::cerr << "Settings file does not exist: " << loadSetupFilename << std::endl;
                    }
                }


                if (doDirectPeerConnect) {
                    auto& state = sonoproc->getValueTreeState();
                    if (auto* sendMute = state.getParameter(SonobusAudioProcessor::paramMainSendMute))
                        sendMute->setValueNotifyingHost(1.0f);
                    if (auto* receiveMute = state.getParameter(SonobusAudioProcessor::paramMainRecvMute))
                        receiveMute->setValueNotifyingHost(0.0f);

                    std::cout << "DIRECT_LOCAL_UDP_PORT=" << sonoproc->getUdpLocalPort() << std::endl;
                    std::cout << "DIRECT_TARGET=" << directPeerHost << ":" << directPeerPort << std::endl;
                    const bool inviteQueued = sonoproc->connectRemotePeer(directPeerHost, directPeerPort, {}, {}, false);
                    std::cout << "DIRECT_INVITE_QUEUED=" << (inviteQueued ? "true" : "false") << std::endl;
                    startTimer(1000);
                }
                else if (doInitialConnect) {
                    DBG("CONNECTING HEADLESS INITIAL");
                    sonoproc->connectToServer(cmdlineConnInfo.serverHost, cmdlineConnInfo.serverPort, cmdlineConnInfo.userName, cmdlineConnInfo.userPassword);

                    // HACK FOR NOW - todo add listener
                    Thread::sleep(500);

                    cmdlineConnInfo.timestamp = Time::getCurrentTime().toMilliseconds();
                    sonoproc->addRecentServerConnectionInfo(cmdlineConnInfo);

                    sonoproc->setWatchPublicGroups(false);

                    sonoproc->joinServerGroup(cmdlineConnInfo.groupName, cmdlineConnInfo.groupPassword, cmdlineConnInfo.groupIsPublic);
                }
            }

        }


#if JUCE_MAC
        disableAppNap();
#endif

    }


    bool loadSettingsFromFile(const File & file)
    {
        SonobusAudioProcessor * processor = nullptr;
        AudioDeviceManager * deviceManager = nullptr;
        StandalonePluginHolder * plugHolder = nullptr;

        if (mainWindow != nullptr && mainWindow->pluginHolder != nullptr) {
            processor = dynamic_cast<SonobusAudioProcessor*>(mainWindow->pluginHolder->processor.get());
            deviceManager = &mainWindow->getDeviceManager();
            plugHolder = mainWindow->pluginHolder.get();
        }
        else if (pluginHolder != nullptr) {
            processor = dynamic_cast<SonobusAudioProcessor*>(pluginHolder->processor.get());
            deviceManager = &pluginHolder->deviceManager;
            plugHolder = pluginHolder.get();
        }

        if (processor == nullptr || deviceManager == nullptr) return false;

        bool retval = true;

        PropertiesFile::Options opts;
        PropertiesFile propfile = PropertiesFile(file, opts);

        if (!propfile.isValidFile()) {
            std::cerr << "Error while loading setup, could not read from the specified file!" << std::endl;

            return false;
        }

        MemoryBlock data;

        if (propfile.containsKey("filterStateXML")) {
            String filtxml = propfile.getValue ("filterStateXML");
            data.replaceWith(filtxml.toUTF8(), filtxml.getNumBytesAsUTF8());
            if (data.getSize() > 0) {
                processor->setStateInformationWithOptions (data.getData(), (int) data.getSize(), false, true, true);
            }
            else {
                DBG("Empty XML filterstate");
                retval = false;
            }
        }
        else {
            if (data.fromBase64Encoding (propfile.getValue ("filterState")) && data.getSize() > 0) {
                processor->setStateInformationWithOptions (data.getData(), (int) data.getSize(), false, true);
            } else {
                retval = false;
            }
        }

        auto savedAudioState = propfile.getXmlValue ("audioSetup");

        if (savedAudioState.get()) {
            std::unique_ptr<AudioDeviceManager::AudioDeviceSetup> prefSetupOptions;
            String preferredDefaultDeviceName;

            // now remove samplerate from saved state if necessary
            // as well as preferredSetupOptions

            if (!((bool)plugHolder->getShouldOverrideSampleRateValue().getValue())) {
                DBG("NOT OVERRIDING SAMPLERATE");
                if (savedAudioState && savedAudioState->hasAttribute("audioDeviceRate")) {
                    savedAudioState->removeAttribute("audioDeviceRate");
                }
                if (prefSetupOptions) {
                    prefSetupOptions->sampleRate = 0;
                }
            }



            auto totalInChannels  = processor->getMainBusNumInputChannels();
            auto totalOutChannels = processor->getMainBusNumOutputChannels();

            deviceManager->initialise (totalInChannels,
                                       totalOutChannels,
                                       savedAudioState.get(),
                                       true,
                                       preferredDefaultDeviceName,
                                       prefSetupOptions.get());
        }

        if (!retval) {
            std::cerr << "Error while loading setup, invalid setup!" << std::endl;
        }

        return retval;
    }

    struct HeadlessPeerSettings
    {
        bool hasJitterBufferMs = false;
        double jitterBufferMs = 0.0;
        bool hasAutoBufferMode = false;
        int autoBufferMode = 0;
        bool hasReceiveGainDb = false;
        double receiveGainDb = 0.0;
        bool hasReceiveFormatIndex = false;
        int receiveFormatIndex = -1;

        bool hasAnySetting() const
        {
            return hasJitterBufferMs || hasAutoBufferMode || hasReceiveGainDb || hasReceiveFormatIndex;
        }
    };

    static File getHeadlessControlDirectory()
    {
        return File::getSpecialLocation(File::userHomeDirectory)
            .getChildFile("Library")
            .getChildFile("Application Support")
            .getChildFile("SonoBusHeadless");
    }

    static bool getFiniteNumber(const var& value, double& result)
    {
        if (value.isBool() || !(value.isInt() || value.isInt64() || value.isDouble()))
            return false;

        result = static_cast<double>(value);
        return std::isfinite(result);
    }

    static bool parseHeadlessPeerSettings(const DynamicObject* object,
                                          SonobusAudioProcessor* processor,
                                          HeadlessPeerSettings& settings,
                                          String& error)
    {
        if (object == nullptr || processor == nullptr)
        {
            error = "invalid_settings";
            return false;
        }

        if (object->hasProperty("jitter_buffer_ms"))
        {
            double value = 0.0;
            if (!getFiniteNumber(object->getProperty("jitter_buffer_ms"), value)
                || value < 10.67 || value > 500.0)
            {
                error = "jitter_buffer_ms_out_of_range";
                return false;
            }
            settings.hasJitterBufferMs = true;
            settings.jitterBufferMs = value;
        }

        if (object->hasProperty("auto_buffer_mode"))
        {
            double value = 0.0;
            if (!getFiniteNumber(object->getProperty("auto_buffer_mode"), value)
                || std::floor(value) != value || value < 0.0 || value > 3.0)
            {
                error = "auto_buffer_mode_out_of_range";
                return false;
            }
            settings.hasAutoBufferMode = true;
            settings.autoBufferMode = static_cast<int>(value);
        }

        if (object->hasProperty("receive_gain_db"))
        {
            double value = 0.0;
            if (!getFiniteNumber(object->getProperty("receive_gain_db"), value)
                || value < -60.0 || value > 6.0)
            {
                error = "receive_gain_db_out_of_range";
                return false;
            }
            settings.hasReceiveGainDb = true;
            settings.receiveGainDb = value;
        }

        if (object->hasProperty("receive_format_index"))
        {
            double value = 0.0;
            const int formatCount = processor->getNumberAudioCodecFormats();
            if (!getFiniteNumber(object->getProperty("receive_format_index"), value)
                || std::floor(value) != value || value < -1.0 || value >= formatCount)
            {
                error = "receive_format_index_out_of_range";
                return false;
            }
            settings.hasReceiveFormatIndex = true;
            settings.receiveFormatIndex = static_cast<int>(value);
        }

        return true;
    }

    static bool applyHeadlessPeerSettings(SonobusAudioProcessor* processor,
                                          int peerIndex,
                                          const HeadlessPeerSettings& settings,
                                          String& error)
    {
        if (processor == nullptr || peerIndex < 0 || peerIndex >= processor->getNumberRemotePeers())
        {
            error = "peer_not_found";
            return false;
        }

        // Request the remote format first so a rejected format leaves the other controls untouched.
        if (settings.hasReceiveFormatIndex
            && !processor->setRequestRemotePeerSendAudioCodecFormat(peerIndex, settings.receiveFormatIndex))
        {
            error = "receive_format_rejected";
            return false;
        }

        if (settings.hasAutoBufferMode)
            processor->setRemotePeerAutoresizeBufferMode(
                peerIndex, (SonobusAudioProcessor::AutoNetBufferMode) settings.autoBufferMode);

        if (settings.hasJitterBufferMs)
            processor->setRemotePeerBufferTime(peerIndex, (float) settings.jitterBufferMs);

        if (settings.hasReceiveGainDb)
            processor->setRemotePeerLevelGain(
                peerIndex, Decibels::decibelsToGain((float) settings.receiveGainDb));

        return true;
    }

    static bool loadSavedHeadlessPeerSettings(const File& preferencesFile,
                                              const String& peerAddress,
                                              SonobusAudioProcessor* processor,
                                              HeadlessPeerSettings& settings,
                                              String& error)
    {
        if (!preferencesFile.existsAsFile())
            return false;

        var rootValue = JSON::parse(preferencesFile.loadFileAsString());
        auto* root = rootValue.getDynamicObject();
        if (root == nullptr)
        {
            error = "preferences_json_invalid";
            return false;
        }

        var peersValue = root->getProperty("peers");
        auto* peers = peersValue.getArray();
        if (peers == nullptr)
        {
            error = "preferences_json_invalid";
            return false;
        }

        for (const auto& peerValue : *peers)
        {
            auto* peer = peerValue.getDynamicObject();
            if (peer == nullptr || peer->getProperty("peer_address").toString() != peerAddress)
                continue;

            return parseHeadlessPeerSettings(peer, processor, settings, error);
        }

        return false;
    }

    static bool saveHeadlessPeerSettings(const File& preferencesFile,
                                         const String& peerAddress,
                                         const HeadlessPeerSettings& settings,
                                         String& error)
    {
        var rootValue;
        if (preferencesFile.existsAsFile())
        {
            rootValue = JSON::parse(preferencesFile.loadFileAsString());
            if (rootValue.getDynamicObject() == nullptr)
            {
                error = "preferences_json_invalid";
                return false;
            }
        }
        else
        {
            rootValue = var(new DynamicObject());
            rootValue.getDynamicObject()->setProperty("version", 1);
            rootValue.getDynamicObject()->setProperty("peers", var(Array<var>()));
        }

        auto* root = rootValue.getDynamicObject();
        var peersValue = root->getProperty("peers");
        auto* peers = peersValue.getArray();
        if (peers == nullptr)
        {
            error = "preferences_json_invalid";
            return false;
        }

        DynamicObject* peer = nullptr;
        for (const auto& peerValue : *peers)
        {
            auto* candidate = peerValue.getDynamicObject();
            if (candidate != nullptr && candidate->getProperty("peer_address").toString() == peerAddress)
            {
                peer = candidate;
                break;
            }
        }

        if (peer == nullptr)
        {
            peer = new DynamicObject();
            peer->setProperty("peer_address", peerAddress);
            peers->add(var(peer));
        }

        if (settings.hasJitterBufferMs)
            peer->setProperty("jitter_buffer_ms", settings.jitterBufferMs);
        if (settings.hasAutoBufferMode)
            peer->setProperty("auto_buffer_mode", settings.autoBufferMode);
        if (settings.hasReceiveGainDb)
            peer->setProperty("receive_gain_db", settings.receiveGainDb);
        if (settings.hasReceiveFormatIndex)
            peer->setProperty("receive_format_index", settings.receiveFormatIndex);

        if (!preferencesFile.getParentDirectory().createDirectory())
        {
            error = "preferences_directory_unavailable";
            return false;
        }

        if (!preferencesFile.replaceWithText(JSON::toString(rootValue, true)))
        {
            error = "preferences_write_failed";
            return false;
        }

        return true;
    }

    int findHeadlessPeerByAddress(SonobusAudioProcessor* processor, const String& peerAddress) const
    {
        if (processor == nullptr)
            return -1;

        for (int i = 0; i < processor->getNumberRemotePeers(); ++i)
        {
            String host;
            int port = 0;
            if (processor->getRemotePeerAddressInfo(i, host, port)
                && host + ":" + String(port) == peerAddress)
                return i;
        }

        return -1;
    }

    static void fillHeadlessPeerAck(DynamicObject* ack,
                                    SonobusAudioProcessor* processor,
                                    int peerIndex,
                                    const String& peerAddress)
    {
        if (ack == nullptr || processor == nullptr || peerIndex < 0
            || peerIndex >= processor->getNumberRemotePeers())
            return;

        ack->setProperty("peer_address", peerAddress);
        ack->setProperty("jitter_buffer_ms", (double) processor->getRemotePeerBufferTime(peerIndex));
        bool initCompleted = false;
        ack->setProperty("auto_buffer_mode", (int) processor->getRemotePeerAutoresizeBufferMode(peerIndex, initCompleted));
        ack->setProperty("gain_linear", (double) processor->getRemotePeerLevelGain(peerIndex));
        ack->setProperty("receive_format_index", processor->getRequestRemotePeerSendAudioCodecFormat(peerIndex));
        ack->setProperty("receive_active", processor->getRemotePeerRecvActive(peerIndex));
        ack->setProperty("receive_allowed", processor->getRemotePeerRecvAllow(peerIndex));
        ack->setProperty("received_bytes", (int64) processor->getRemotePeerBytesReceived(peerIndex));
        ack->setProperty("dropped_packets", (int64) processor->getRemotePeerPacketsDropped(peerIndex));

        SonobusAudioProcessor::LatencyInfo latency;
        if (processor->getRemotePeerLatencyInfo(peerIndex, latency))
        {
            ack->setProperty("ping_rtt_ms", std::isfinite(latency.pingMs) ? (double) latency.pingMs : 0.0);
            ack->setProperty("reported_jitter_ms", std::isfinite(latency.jitterMs) ? (double) latency.jitterMs : 0.0);
        }

        SonobusAudioProcessor::AudioCodecFormatInfo codec;
        if (processor->getRemotePeerReceiveAudioCodecFormat(peerIndex, codec))
        {
            ack->setProperty("receive_codec", codec.name);
            ack->setProperty("receive_codec_kind", codec.codec == SonobusAudioProcessor::CodecOpus ? "Opus" : "PCM");
            ack->setProperty("receive_codec_bitrate", codec.bitrate);
            ack->setProperty("receive_codec_bitdepth_bytes", codec.bitdepth);
        }
    }

    void applySavedHeadlessPeerSettings(SonobusAudioProcessor* processor, const File& controlDirectory)
    {
        if (processor == nullptr)
            return;

        StringArray connectedAddresses;
        const File preferencesFile = controlDirectory.getChildFile("headless-peer-settings.json");

        for (int i = 0; i < processor->getNumberRemotePeers(); ++i)
        {
            String host;
            int port = 0;
            if (!processor->getRemotePeerAddressInfo(i, host, port))
                continue;

            const String peerAddress = host + ":" + String(port);
            if (!processor->getRemotePeerConnected(i))
            {
                headlessAppliedPeerAddresses.removeString(peerAddress);
                continue;
            }

            connectedAddresses.addIfNotAlreadyThere(peerAddress);
            if (headlessAppliedPeerAddresses.contains(peerAddress))
                continue;

            HeadlessPeerSettings settings;
            String error;
            if (loadSavedHeadlessPeerSettings(preferencesFile, peerAddress, processor, settings, error)
                && settings.hasAnySetting())
            {
                if (!applyHeadlessPeerSettings(processor, i, settings, error))
                    std::cerr << "HEADLESS_CONTROL_REAPPLY_FAILED peer=" << peerAddress.toStdString()
                              << " error=" << error.toStdString() << std::endl;
            }
            else if (error.isNotEmpty())
            {
                std::cerr << "HEADLESS_CONTROL_PREFERENCES_INVALID peer=" << peerAddress.toStdString()
                          << " error=" << error.toStdString() << std::endl;
            }

            headlessAppliedPeerAddresses.addIfNotAlreadyThere(peerAddress);
        }

        for (int i = headlessAppliedPeerAddresses.size(); --i >= 0;)
            if (!connectedAddresses.contains(headlessAppliedPeerAddresses[i]))
                headlessAppliedPeerAddresses.remove(i);
    }

    void processHeadlessControlRequest(SonobusAudioProcessor* processor, const File& controlDirectory)
    {
        const File requestFile = controlDirectory.getChildFile("headless-control-request.json");
        if (!requestFile.existsAsFile())
            return;

        const File processingFile = controlDirectory.getChildFile(
            "headless-control-request.processing-" + String(Time::getHighResolutionTicks()) + ".json");
        if (!requestFile.moveFileTo(processingFile))
            return;

        var requestValue = JSON::parse(processingFile.loadFileAsString());
        auto* request = requestValue.getDynamicObject();
        auto* ack = new DynamicObject();
        var ackValue(ack);

        ack->setProperty("request_id", "");
        ack->setProperty("ok", false);
        ack->setProperty("error", "invalid_json");
        ack->setProperty("peer_address", "");
        ack->setProperty("jitter_buffer_ms", 0.0);
        ack->setProperty("auto_buffer_mode", 0);
        ack->setProperty("gain_linear", 0.0);
        ack->setProperty("receive_format_index", -1);
        ack->setProperty("receive_codec", "");
        ack->setProperty("receive_codec_kind", "");
        ack->setProperty("receive_codec_bitrate", 0);
        ack->setProperty("receive_codec_bitdepth_bytes", 0);
        ack->setProperty("receive_active", false);
        ack->setProperty("receive_allowed", false);
        ack->setProperty("received_bytes", (int64) 0);
        ack->setProperty("dropped_packets", (int64) 0);
        ack->setProperty("ping_rtt_ms", 0.0);
        ack->setProperty("reported_jitter_ms", 0.0);
        ack->setProperty("settings_persisted", false);
        ack->setProperty("applied", false);

        String error("invalid_json");
        String requestId;
        String peerAddress;
        int peerIndex = -1;
        bool query = false;
        bool requestSucceeded = false;
        HeadlessPeerSettings settings;

        if (request != nullptr)
        {
            const var requestIdValue = request->getProperty("request_id");
            const var peerAddressValue = request->getProperty("peer_address");
            if (!requestIdValue.isString() || requestIdValue.toString().isEmpty()
                || requestIdValue.toString().length() > 128)
                error = "invalid_request_id";
            else
            {
                requestId = requestIdValue.toString();
                if (!peerAddressValue.isString() || peerAddressValue.toString().isEmpty()
                    || peerAddressValue.toString().length() > 255)
                    error = "invalid_peer_address";
                else
                {
                    peerAddress = peerAddressValue.toString();
                    const var queryValue = request->getProperty("query");
                    if (!queryValue.isVoid() && !queryValue.isBool())
                        error = "invalid_query_flag";
                    else
                    {
                        query = queryValue.isBool() && (bool) queryValue;
                        const bool hasSettings = request->hasProperty("jitter_buffer_ms")
                            || request->hasProperty("auto_buffer_mode")
                            || request->hasProperty("receive_gain_db")
                            || request->hasProperty("receive_format_index");

                        if (query && hasSettings)
                            error = "query_cannot_change_settings";
                        else if (!query && !hasSettings)
                            error = "no_settings";
                        else if (!query && !parseHeadlessPeerSettings(request, processor, settings, error))
                        {
                            // parseHeadlessPeerSettings supplies the validation error.
                        }
                        else
                        {
                            peerIndex = findHeadlessPeerByAddress(processor, peerAddress);
                            if (peerIndex < 0)
                                error = "peer_not_found";
                            else
                            {
                                fillHeadlessPeerAck(ack, processor, peerIndex, peerAddress);
                                error = "";
                                if (query)
                                    requestSucceeded = true;
                                else if (applyHeadlessPeerSettings(processor, peerIndex, settings, error))
                                {
                                    ack->setProperty("applied", true);
                                    headlessAppliedPeerAddresses.addIfNotAlreadyThere(peerAddress);

                                    const File preferencesFile = controlDirectory.getChildFile("headless-peer-settings.json");
                                    String persistError;
                                    const bool persisted = saveHeadlessPeerSettings(preferencesFile, peerAddress, settings, persistError);
                                    ack->setProperty("settings_persisted", persisted);
                                    if (persisted)
                                        requestSucceeded = true;
                                    else
                                        error = persistError;
                                }

                                fillHeadlessPeerAck(ack, processor, peerIndex, peerAddress);
                            }
                        }
                    }
                }
            }
        }

        ack->setProperty("request_id", requestId);
        ack->setProperty("peer_address", peerAddress);
        ack->setProperty("ok", requestSucceeded);
        if (!requestSucceeded)
            ack->setProperty("error", error.isEmpty() ? "request_failed" : error);
        else
            ack->setProperty("error", "");

        const File ackFile = controlDirectory.getChildFile("headless-control-ack.json");
        if (!ackFile.replaceWithText(JSON::toString(ackValue, true)))
            std::cerr << "HEADLESS_CONTROL_ACK_WRITE_FAILED path=" << ackFile.getFullPathName().toStdString() << std::endl;

        processingFile.deleteFile();
    }


    void timerCallback() override
    {
        if (doHeadless && doDirectPeerConnect && pluginHolder != nullptr) {
            if (auto* proc = dynamic_cast<SonobusAudioProcessor*>(pluginHolder->processor.get())) {
                const File controlDirectory = getHeadlessControlDirectory();
                applySavedHeadlessPeerSettings(proc, controlDirectory);
                processHeadlessControlRequest(proc, controlDirectory);

                const File exportRequest("/tmp/sonobus-export-request");
                if (exportRequest.existsAsFile()) {
                    MemoryBlock savedState;
                    proc->getStateInformationWithOptions(savedState, false, true, true);
                    XmlElement savedSetup("PROPERTIES");
                    auto* stateValue = savedSetup.createNewChildElement("VALUE");
                    stateValue->setAttribute("name", "filterStateXML");
                    if (auto xml = XmlDocument::parse(String::fromUTF8(static_cast<const char*>(savedState.getData())))) {
                        for (auto* parameter : proc->getParameters()) {
                            if (auto* ranged = dynamic_cast<RangedAudioParameter*>(parameter)) {
                                auto* node = xml->getChildByAttribute("id", ranged->paramID);
                                if (node == nullptr) {
                                    node = xml->createNewChildElement("PARAM");
                                    node->setAttribute("id", ranged->paramID);
                                }
                                node->setAttribute("value", ranged->convertFrom0to1(ranged->getValue()));
                            }
                        }
                        stateValue->addChildElement(xml.release());
                    }
                    auto* deviceValue = savedSetup.createNewChildElement("VALUE");
                    deviceValue->setAttribute("name", "audioSetup");
                    if (auto xml = pluginHolder->deviceManager.createStateXml())
                        deviceValue->addChildElement(xml.release());
                    auto* root = new DynamicObject();
                    var runtime(root);
                    root->setProperty("timestamp", Time::getCurrentTime().toISO8601(true));
                    root->setProperty("version", getApplicationVersion());
                    root->setProperty("headless", true);
                    root->setProperty("direct_target", directPeerHost + ":" + String(directPeerPort));
                    root->setProperty("local_udp_port", proc->getUdpLocalPort());
                    AudioDeviceManager::AudioDeviceSetup devSetup;
                    pluginHolder->deviceManager.getAudioDeviceSetup(devSetup);
                    root->setProperty("input_device", devSetup.inputDeviceName);
                    root->setProperty("output_device", devSetup.outputDeviceName);
                    root->setProperty("device_type", pluginHolder->deviceManager.getCurrentAudioDeviceType());
                    if (auto* device = pluginHolder->deviceManager.getCurrentAudioDevice()) {
                        const double sr = device->getCurrentSampleRate();
                        root->setProperty("sample_rate_hz", sr);
                        root->setProperty("buffer_samples", device->getCurrentBufferSizeSamples());
                        root->setProperty("buffer_ms", 1000.0 * device->getCurrentBufferSizeSamples() / sr);
                        root->setProperty("input_channels", device->getActiveInputChannels().countNumberOfSetBits());
                        root->setProperty("output_channels", device->getActiveOutputChannels().countNumberOfSetBits());
                        root->setProperty("device_input_latency_samples", device->getInputLatencyInSamples());
                        root->setProperty("device_output_latency_samples", device->getOutputLatencyInSamples());
                        root->setProperty("device_input_latency_ms", 1000.0 * device->getInputLatencyInSamples() / sr);
                        root->setProperty("device_output_latency_ms", 1000.0 * device->getOutputLatencyInSamples() / sr);
                    }
                    Array<var> peers;
                    for (int peerIndex = 0; peerIndex < proc->getNumberRemotePeers(); ++peerIndex) {
                        auto* peer = new DynamicObject();
                        String host; int port = 0;
                        proc->getRemotePeerAddressInfo(peerIndex, host, port);
                        peer->setProperty("address", host + ":" + String(port));
                        peer->setProperty("receive_channels", proc->getRemotePeerRecvChannelCount(peerIndex));
                        peer->setProperty("receive_allowed", proc->getRemotePeerRecvAllow(peerIndex));
                        peer->setProperty("receive_active", proc->getRemotePeerRecvActive(peerIndex));
                        peer->setProperty("send_allowed", proc->getRemotePeerSendAllow(peerIndex));
                        peer->setProperty("send_active", proc->getRemotePeerSendActive(peerIndex));
                        peer->setProperty("safety_muted", proc->getRemotePeerSafetyMuted(peerIndex));
                        peer->setProperty("gain_linear", proc->getRemotePeerLevelGain(peerIndex));
                        peer->setProperty("jitter_buffer_ms", proc->getRemotePeerBufferTime(peerIndex));
                        bool initCompleted = false;
                        peer->setProperty("auto_buffer_mode", (int)proc->getRemotePeerAutoresizeBufferMode(peerIndex, initCompleted));
                        peer->setProperty("auto_buffer_initialization_complete", initCompleted);
                        peer->setProperty("received_bytes", (int64)proc->getRemotePeerBytesReceived(peerIndex));
                        peer->setProperty("sent_bytes", (int64)proc->getRemotePeerBytesSent(peerIndex));
                        peer->setProperty("dropped_packets", (int64)proc->getRemotePeerPacketsDropped(peerIndex));
                        SonobusAudioProcessor::LatencyInfo latency;
                        if (proc->getRemotePeerLatencyInfo(peerIndex, latency)) {
                            peer->setProperty("ping_rtt_ms", latency.pingMs);
                            peer->setProperty("reported_roundtrip_ms", latency.totalRoundtripMs);
                            peer->setProperty("reported_incoming_ms", latency.incomingMs);
                            peer->setProperty("reported_outgoing_ms", latency.outgoingMs);
                            peer->setProperty("reported_jitter_ms", latency.jitterMs);
                            peer->setProperty("latency_is_real_measurement", latency.isreal);
                            peer->setProperty("latency_is_estimate", latency.estimated);
                        }
                        SonobusAudioProcessor::AudioCodecFormatInfo codec;
                        if (proc->getRemotePeerReceiveAudioCodecFormat(peerIndex, codec)) {
                            peer->setProperty("receive_codec", codec.name);
                            peer->setProperty("receive_codec_kind", codec.codec == SonobusAudioProcessor::CodecOpus ? "Opus" : "PCM");
                            peer->setProperty("receive_codec_bitrate", codec.bitrate);
                            peer->setProperty("receive_codec_bitdepth_bytes", codec.bitdepth);
                        }
                        peers.add(var(peer));
                    }
                    root->setProperty("peers", var(peers));
                    const bool xmlOk = File("/tmp/sonobus-current-full-setup.xml").replaceWithText(savedSetup.toString());
                    const bool jsonOk = File("/tmp/sonobus-core-runtime.json").replaceWithText(JSON::toString(runtime, false));
                    std::cout << "DIRECT_EXPORT_OK=" << (xmlOk && jsonOk) << std::endl;
                    exportRequest.deleteFile();
                }
                for (int i = 0; i < proc->getNumberRemotePeers(); ++i) {
                    std::cout << "DIRECT_RX"
                              << " dry=" << proc->getValueTreeState().getRawParameterValue(SonobusAudioProcessor::paramDry)->load()
                              << " send_mute=" << proc->getValueTreeState().getRawParameterValue(SonobusAudioProcessor::paramMainSendMute)->load()
                              << " input_mute=" << proc->getValueTreeState().getRawParameterValue(SonobusAudioProcessor::paramMainInMute)->load()
                              << " input_channels=" << (pluginHolder->deviceManager.getCurrentAudioDevice() ? pluginHolder->deviceManager.getCurrentAudioDevice()->getActiveInputChannels().countNumberOfSetBits() : -1)
                              << " device=" << (pluginHolder->deviceManager.getCurrentAudioDevice() ? pluginHolder->deviceManager.getCurrentAudioDevice()->getName().toStdString() : "none")
                              << " peer=" << i
                              << " allow=" << proc->getRemotePeerRecvAllow(i)
                              << " active=" << proc->getRemotePeerRecvActive(i)
                              << " safety_mute=" << proc->getRemotePeerSafetyMuted(i)
                              << " channels=" << proc->getRemotePeerRecvChannelCount(i)
                              << " bytes=" << proc->getRemotePeerBytesReceived(i);
                    if (auto* meter = proc->getRemotePeerRecvMeterSource(i)) {
                        for (int ch = 0; ch < meter->getNumChannels(); ++ch)
                            std::cout << " ch" << ch << "_peak_dbfs="
                                      << Decibels::gainToDecibels(meter->getMaxLevel(ch), -100.0f)
                                      << " ch" << ch << "_rms_dbfs="
                                      << Decibels::gainToDecibels(meter->getRMSLevel(ch), -100.0f);
                    }
                    auto& out = proc->getOutputMeterSource();
                    for (int ch = 0; ch < out.getNumChannels(); ++ch)
                        std::cout << " out" << ch << "_rms_dbfs="
                                  << Decibels::gainToDecibels(out.getRMSLevel(ch), -100.0f);
                    std::cout << std::endl;
                }
            }
        }
#if JUCE_ANDROID
        DBG("setting foreground service active");
        setAndroidForegroundServiceActive(true);
        stopTimer();
#endif
    }

    void shutdown() override
    {
        //DBG("shutdown");
        if (mainWindow.get() != nullptr) {
            mainWindow->pluginHolder->savePluginState();
            mainWindow->pluginHolder->saveAudioDeviceState();
        }

#if JUCE_ANDROID
        setAndroidForegroundServiceActive(false);
  #if JUCE_OPENGL
        detachGL();
  #endif
#endif

        mainWindow = nullptr;

        pluginHolder = nullptr;

        appProperties.saveIfNeeded();

    }
    
    void urlOpened(const URL & url) override {
        DBG("Url opened: " << url.toString(true));
        
        if (mainWindow.get() != nullptr) {
            
            if (auto * sonoproc = dynamic_cast<SonobusAudioProcessor*>(mainWindow->pluginHolder->processor.get())) {
                if (sonoproc->hasEditor()) {
                    if (auto * sonoeditor = dynamic_cast<SonobusAudioProcessorEditor*>(sonoproc->createEditorIfNeeded())) {
                        sonoeditor->handleURL(url.toString(true));
                        mainWindow->toFront(true);
                    }
                }
            }
        }        
    }
    
    void anotherInstanceStarted (const String& url) override    {
        
        DBG("Url handled from another instance: " << url);
        
        if (mainWindow.get() != nullptr) {
            
            if (auto * sonoproc = dynamic_cast<SonobusAudioProcessor*>(mainWindow->pluginHolder->processor.get())) {
                if (sonoproc->hasEditor()) {
                    if (auto * sonoeditor = dynamic_cast<SonobusAudioProcessorEditor*>(sonoproc->createEditorIfNeeded())) {
                        sonoeditor->handleURL(url);
                        mainWindow->toFront(true);
                    }
                }
            }
        }
    }
        
    void suspended() override
    {
        DBG("suspended");
        if (mainWindow.get() != nullptr) {
            mainWindow->pluginHolder->savePluginState();
            mainWindow->pluginHolder->saveAudioDeviceState();

            if (auto * sonoproc = dynamic_cast<SonobusAudioProcessor*>(mainWindow->pluginHolder->processor.get())) {
                if (sonoproc->getNumberRemotePeers() == 0 && !mainWindow->pluginHolder->isInterAppAudioConnected()) {
                    // shutdown audio engine
                    DBG("no connections shutting down audio");
                    mainWindow->getDeviceManager().closeAudioDevice();
                    sonoproc->setPlayHead (nullptr);
#if JUCE_ANDROID
                    setAndroidForegroundServiceActive(false);
#endif
                }
                else {
#if JUCE_ANDROID
                    setAndroidForegroundServiceActive(true);
#endif
                }
            }

        }

        appProperties.saveIfNeeded();

        auto props = appProperties.getUserSettings()->getAllProperties();
        for (auto & prop : props.getAllKeys()) {
            DBG("save state key: " << prop );
        }
        
        Desktop::getInstance().setScreenSaverEnabled(true);        
    }

    void setAndroidForegroundServiceActive(bool flag)
    {
    #if JUCE_ANDROID
        LocalRef<jobject> activity (getMainActivity());

        if (activity != nullptr) {
            getEnv()->CallVoidMethod(activity.get(), SonoBusActivity.setForegroundServiceActive, flag);
        }
    #endif
    }


    void resumed() override
    {
        Desktop::getInstance().setScreenSaverEnabled(false);
        
#if JUCE_STANDALONE_FILTER_WINDOW_USE_KIOSK_MODE
        Desktop::getInstance().setKioskModeComponent (nullptr, false);
        Desktop::getInstance().setKioskModeComponent (mainWindow.get(), false);
#endif

        if (auto * dev = mainWindow->getDeviceManager().getCurrentAudioDevice()) {
            if (!dev->isPlaying()) {
                DBG("dev not playing, restarting");
                mainWindow->getDeviceManager().restartLastAudioDevice();
            }
        }
        else {
            DBG("was not active: restarting");
            mainWindow->getDeviceManager().restartLastAudioDevice();
        }

#if JUCE_ANDROID
        setAndroidForegroundServiceActive(true);
#endif

    }
    
    //==============================================================================
    void systemRequestedQuit() override
    {
        DBG("Requested quit");
        if (mainWindow.get() != nullptr) {
            mainWindow->pluginHolder->savePluginState();
            mainWindow->pluginHolder->saveAudioDeviceState();
        }

        appProperties.saveIfNeeded();

        
        if (mainWindow.get() != nullptr) {
            if (auto * editor = mainWindow->getEditor()) {
                if (auto * sonoeditor = dynamic_cast<SonobusAudioProcessorEditor*>(editor)) {
                    if (!sonoeditor->requestedQuit()) {
                        // they'll handle it
                        return;
                    }
                }
            }
        }
        
        if (ModalComponentManager::getInstance()->cancelAllModalComponents())
        {
            Timer::callAfterDelay (100, []()
            {
                if (auto app = JUCEApplicationBase::getInstance())
                    app->systemRequestedQuit();
            });
        }
        else
        {
            quit();
        }
    }

    void memoryWarningReceived()  override
    {
        DBG("Memory warning");
    }

    void attachGL()
    {
        if (mainWindow) {
#if JUCE_OPENGL  && JUCE_ANDROID
            DBG("activating GL context");
            openGLContext.makeActive();
            DBG("OPEN GL attaching to " << (long) mainWindow.get());
            openGLContext.attachTo (*mainWindow);
#endif
        }
    }

    void detachGL()
    {

    #if JUCE_OPENGL && JUCE_ANDROID
        DBG("About to detach opengl context");
        if (openGLContext.isAttached()) {
            openGLContext.detach();
            DBG("About to deactivate opengl context");
            openGLContext.deactivateCurrentContext();
            DBG("done detach opengl context");
        }
    #endif
    }

protected:

#if JUCE_OPENGL
    OpenGLContext openGLContext;
#endif

    ApplicationProperties appProperties;
    std::unique_ptr<StandaloneFilterWindow> mainWindow;

    // used only in headless mode
    std::unique_ptr<StandalonePluginHolder> pluginHolder;

};

} // namespace juce

#if JucePlugin_Build_Standalone && JUCE_IOS

using namespace juce;

bool JUCE_CALLTYPE juce_isInterAppAudioConnected()
{
    if (auto holder = StandalonePluginHolder::getInstance())
        return holder->isInterAppAudioConnected();

    return false;
}

void JUCE_CALLTYPE juce_switchToHostApplication()
{
    if (auto holder = StandalonePluginHolder::getInstance())
        holder->switchToHostApplication();
}

#if JUCE_MODULE_AVAILABLE_juce_gui_basics
Image JUCE_CALLTYPE juce_getIAAHostIcon (int size)
{
    if (auto holder = StandalonePluginHolder::getInstance())
        return holder->getIAAHostIcon (size);

    return Image();
}
#endif
#endif

START_JUCE_APPLICATION (SonobusStandaloneFilterApp);

//#endif
