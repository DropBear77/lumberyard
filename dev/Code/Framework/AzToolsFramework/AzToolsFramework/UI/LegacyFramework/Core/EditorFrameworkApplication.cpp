/*
* All or portions of this file Copyright (c) Amazon.com, Inc. or its affiliates or
* its licensors.
*
* For complete copyright and license terms please see the LICENSE at the root of this
* distribution (the "License"). All use of this software is governed by the License,
* or, if provided, by the license below or the license accompanying this file. Do not
* remove or modify any license notices. This file is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
*
*/

#include "StdAfx.h"
#include "EditorFrameworkApplication.h"

#include <time.h>

#include <AzCore/Memory/OSAllocator.h>
#include <AzCore/IO/Streamer.h>
#include <AzCore/IO/SystemFile.h>
#include <AzCore/IO/GenericStreams.h>
#include <AzCore/Serialization/ObjectStream.h>
#include <AzCore/Debug/trace.h>
#include <AzCore/Math/Sfmt.h>
#include <AzCore/std/time.h>

#include <AzCore/Component/ComponentApplication.h>
#include <AzCore/Memory/MemoryComponent.h>
#include <AzCore/Jobs/JobManagerComponent.h>
#include <AzCore/Serialization/ObjectStreamComponent.h>
#include <AzCore/Asset/AssetManagerComponent.h>
#include <AzCore/Script/ScriptSystemComponent.h>
#include <AzCore/IO/StreamerComponent.h>
#include <AzCore/IO/StreamerRequest.h>

#include <AzFramework/CommandLine/CommandLine.h>

#include <AzToolsFramework/UI/LegacyFramework/UIFramework.hxx>
#include <AzToolsFramework/UI/LegacyFramework/CustomMenus/CustomMenusAPI.h>

#include <AzFramework/Asset/AssetCatalogComponent.h>
#include <AzFramework/Components/BootstrapReaderComponent.h>
#include <AzFramework/StringFunc/StringFunc.h>
#include <AzFramework/TargetManagement/TargetManagementComponent.h>
#include <AzFramework/Driller/RemoteDrillerInterface.h>

#include <AzCore/Driller/Driller.h>
#include <AzCore/Debug/ProfilerDriller.h>

#ifdef AZ_PLATFORM_WINDOWS
#include "shlobj.h"
#endif

#if defined(AZ_PLATFORM_APPLE)
#   include <mach-o/dyld.h>
#endif

#include <QFileInfo>
#include <QSharedMemory>
#include <QStandardPaths>

namespace LegacyFramework
{
    ApplicationDesc::ApplicationDesc(const char* name)
        : m_applicationModule(NULL)
        , m_enableGridmate(true)
        , m_enablePerforce(true)
        , m_enableGUI(true)
        , m_enableProjectManager(true)
        , m_shouldRunAssetProcessor(true)
        , m_saveUserSettings(true)
    {
        m_applicationName[0] = 0;
        if (name)
        {
            qstrcpy(m_applicationName, name);
        }
    }

    ApplicationDesc::ApplicationDesc(const ApplicationDesc& other)
    {
        this->operator=(other);
    }

    ApplicationDesc& ApplicationDesc::operator=(const ApplicationDesc& other)
    {
        if (this == &other)
        {
            return *this;
        }

        m_applicationModule = other.m_applicationModule;
        m_enableGUI = other.m_enableGUI;
        m_enableGridmate = other.m_enableGridmate;
        m_enablePerforce = other.m_enablePerforce;
        qstrcpy(m_applicationName, other.m_applicationName);
        m_enableProjectManager = other.m_enableProjectManager;
        m_shouldRunAssetProcessor = other.m_shouldRunAssetProcessor;
        m_saveUserSettings = other.m_saveUserSettings;
        return *this;
    }

    Application::Application()
    {
        m_isMaster = true;
        m_desiredExitCode = 0;
        m_abortRequested = false;
        m_applicationEntity = NULL;
        m_ptrSystemEntity = NULL;
        m_applicationModule[0] = 0;
        m_appRoot[0] = 0;
    }

    HMODULE Application::GetMainModule()
    {
        return m_desc.m_applicationModule;
    }


    const char* Application::GetApplicationName()
    {
        return m_desc.m_applicationName;
    }

    const char* Application::GetApplicationModule()
    {
        return m_applicationModule;
    }


    const char* Application::GetApplicationDirectory()
    {
        return GetExecutableFolder();
    }

#ifdef AZ_PLATFORM_WINDOWS
    BOOL CTRL_BREAK_HandlerRoutine(DWORD /*dwCtrlType*/)
    {
        EBUS_EVENT(FrameworkApplicationMessages::Bus, SetAbortRequested);
        return TRUE;
    }
#endif

    AZStd::string Application::GetApplicationGlobalStoragePath()
    {
        return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation).toUtf8().data();
    }

    Application::~Application()
    {
    }


    const char* Application::GetAppRoot()
    {
        return m_appRoot;
    }


    int Application::Run(const ApplicationDesc& desc)
    {
        if (!AZ::AllocatorInstance<AZ::OSAllocator>::IsReady())
        {
            AZ::AllocatorInstance<AZ::OSAllocator>::Create();
        }

        QString appNameConcat = QStringLiteral("%1_GLOBALMUTEX").arg(desc.m_applicationName);
        {
            // If the application crashed before, it may have left behind shared memory
            // We can go ahead and do a quick attach/deatch here to clean it up
            QSharedMemory fix(appNameConcat);
            fix.attach();
        }
        QSharedMemory* shared = new QSharedMemory(appNameConcat);
        if (qApp)
        {
            QObject::connect(qApp, &QCoreApplication::aboutToQuit, [shared]() { delete shared; });
        }
        m_isMaster = shared->create(1);

        m_desc = desc;
        // before we connect to the app bus, find out all the various stuff we need to cache:
#ifdef AZ_PLATFORM_WINDOWS
        char szDir[_MAX_DIR];
        char szDrv[_MAX_DRIVE];
#endif
        char configFilePath[_MAX_PATH];

        CalculateExecutablePath();

#ifdef AZ_PLATFORM_WINDOWS
        GetModuleFileNameA(desc.m_applicationModule, m_applicationModule, _MAX_PATH);
        ::_splitpath_s(m_applicationModule, szDrv, _MAX_DRIVE, szDir, _MAX_DIR, NULL, 0, NULL, 0);

        ::_makepath_s(configFilePath, _MAX_PATH, szDrv, szDir, desc.m_applicationName, ".xml");
#else
        uint32_t bufSize = AZ_ARRAY_SIZE(m_applicationModule);
        _NSGetExecutablePath(m_applicationModule, &bufSize);
        QString path = QStringLiteral("%1/%2.xml").arg(m_exeDirectory, desc.m_applicationName);
        qstrcpy(configFilePath, path.toUtf8().data());
#endif

        // Enable next line to load from the last state
        // if left commented, we will start from scratch
        // THE FOLLOWING LINE CREATES THE MEMORY MANAGER SUBSYSTEM, do not allocate memory before this call!
        m_ptrSystemEntity = Create(configFilePath);

        AZ::Debug::Trace::HandleExceptions(false);
        FrameworkApplicationMessages::Handler::BusConnect();
        CoreMessageBus::Handler::BusConnect();

#ifdef AZ_PLATFORM_WINDOWS
        // if we're in console mode, listen for CTRL+C
        ::SetConsoleCtrlHandler(CTRL_BREAK_HandlerRoutine, true);
#endif

        // Initialize the app root to the exe folder
        azstrncpy(m_appRoot, AZ_MAX_PATH_LEN, m_exeDirectory, AZ_MAX_PATH_LEN);

        m_ptrCommandLineParser = aznew AzFramework::CommandLine();
        m_ptrCommandLineParser->Parse();
        if (m_ptrCommandLineParser->HasSwitch("app-root"))
        {
            auto appRootOverride = m_ptrCommandLineParser->GetSwitchValue("app-root", 0);
            if (!appRootOverride.empty())
            {
                azstrncpy(m_appRoot, AZ_MAX_PATH_LEN, appRootOverride.c_str(), AZ_MAX_PATH_LEN);
            }
        }

        // If we don't have one create a serialize context
        if (GetSerializeContext() == nullptr)
        {
            CreateReflectionManager();
        }

        CreateSystemComponents();

        m_ptrSystemEntity->Init();
        m_ptrSystemEntity->Activate();

        // If we aren't the master, RunAsAnotherInstance unless we are being forcestarted
        if (!m_isMaster && !m_ptrCommandLineParser->HasSwitch("forcestart"))
        {
            // Required for the application component to handle RunAsAnotherInstance
            CreateApplicationComponent();

            // if we're not the master instance, what exactly do we do?  This is a generic framework - not a specific app
            // and what we do might depend on implementation specifics for each app.
            EBUS_EVENT(LegacyFramework::CoreMessageBus, RunAsAnotherInstance);
        }
        else
        {
            //if (!RequiresGameProject())
            {
                // we won't be getting the project set message
                CreateApplicationComponent();
            }

            EBUS_EVENT(LegacyFramework::CoreMessageBus, Run);

            // as a precaution here, we save our app and system entities BEFORE we destroy anything
            // so that we have the highest chance of storing the user's precious application state and preferences
            // even if someone has done something bad on their destructor or shutdown
            SaveApplicationEntity();

            if (m_ptrSystemEntity)
            {
                // Write the current state of the system components into cfg file.
                // unless its not writeable!
                QFileInfo fileAttribs(configFilePath);
                bool writeIt = true;
                if (fileAttribs.exists())
                {
                    // file is found.
                    if (fileAttribs.isHidden() || !fileAttribs.isWritable())
                    {
                        writeIt = false;
                    }
                }

                if (writeIt)
                {
                    WriteApplicationDescriptor(configFilePath);
                }
            }
        }

        if (m_applicationEntity)
        {
            m_applicationEntity->Deactivate();
            delete m_applicationEntity;
            m_applicationEntity = NULL;
        }

        AZ::SystemTickBus::ExecuteQueuedEvents();
        AZ::TickBus::ExecuteQueuedEvents();

#ifdef AZ_PLATFORM_WINDOWS
        // clean up!
        ::SetConsoleCtrlHandler(CTRL_BREAK_HandlerRoutine, false);
#endif

        delete m_ptrCommandLineParser;
        m_ptrCommandLineParser = NULL;

        CoreMessageBus::Handler::BusDisconnect();
        FrameworkApplicationMessages::Handler::BusDisconnect();

        m_ptrSystemEntity->Deactivate();

        Destroy();

        return GetDesiredExitCode();
    }

    void Application::TeardownApplicationComponent()
    {
        SaveApplicationEntity();

        if (m_applicationEntity)
        {
            m_applicationEntity->Deactivate();
            delete m_applicationEntity;
            m_applicationEntity = NULL;
        }
    }

    const AzFramework::CommandLine* Application::GetCommandLineParser()
    {
        return m_ptrCommandLineParser;
    }

    // returns TRUE if the component already existed, FALSE if it had to create one.
    bool Application::EnsureComponentCreated(AZ::Uuid componentCRC)
    {
        if (m_applicationEntity)
        {
            // if the component already exists on the system entity, this is an error.
            if (auto comp = m_ptrSystemEntity->FindComponent(componentCRC))
            {
                AZ_Warning("EditorFramework", 0, "Attempt to add a component that already exists on the system entity: %s\n", comp->RTTI_TypeName());
                return true;
            }


            if (!m_applicationEntity->FindComponent(componentCRC))
            {
                if (m_applicationEntity->GetState() != AZ::Entity::ES_CONSTRUCTED)
                {
                    AZ_Warning("EditorFramework", 0, "Attempt to add a component 0x%08x to the application entity when the application entity has already been activated\n", componentCRC);
                    return false;
                }
                m_applicationEntity->CreateComponent(componentCRC);
            }

            return true;
        }

        if (m_ptrSystemEntity->GetState() != AZ::Entity::ES_CONSTRUCTED)
        {
            AZ_Warning("EditorFramework", 0, "Attempt to add a component 0x%08x to the system entity when the system entity has already been activated\n", componentCRC);
            return false;
        }

        if (!m_ptrSystemEntity->FindComponent(componentCRC))
        {
            m_ptrSystemEntity->CreateComponent(componentCRC);
            return false;
        }
        return true;
    }

    // returns TRUE if the component existed, FALSE if the component did not exist.
    bool Application::EnsureComponentRemoved(AZ::Uuid componentCRC)
    {
        if (m_applicationEntity)
        {
            if (auto comp = m_applicationEntity->FindComponent(componentCRC))
            {
                if (m_applicationEntity->GetState() != AZ::Entity::ES_CONSTRUCTED)
                {
                    AZ_Warning("EditorFramework", 0, "Attempt to remove a component %s (0x%08x) from the application entity when the application entity has already been activated\n", comp->RTTI_GetTypeName(), componentCRC);
                    return true;
                }
                m_applicationEntity->RemoveComponent(comp);
                delete comp;
                return true;
            }
            return false;
        }

        if (m_ptrSystemEntity)
        {
            if (auto comp = m_ptrSystemEntity->FindComponent(componentCRC))
            {
                if (m_ptrSystemEntity->GetState() != AZ::Entity::ES_CONSTRUCTED)
                {
                    AZ_Warning("EditorFramework", 0, "Attempt to remove a component %s (0x%08x) from the system entity when the entity entity has already been activated\n", comp->RTTI_GetTypeName(), componentCRC);
                    return true;
                }
                m_ptrSystemEntity->RemoveComponent(comp);
                delete comp;
                return true;
            }
        }

        return false;
    }

    void Application::CreateApplicationComponent()
    {
        // create the application entity:
        if (m_applicationEntity)
        {
            return;
        }
        AZ_TracePrintf("EditorFramework", "Application::OnProjectSet -- Creating Application Entity.\n");
        AZ_Assert(m_applicationEntity == nullptr, "Attempt to set a project while the project is still set.");

        AZStd::string applicationFilePath;
        AzFramework::StringFunc::Path::Join(GetExecutableFolder(), appName(), applicationFilePath);

        applicationFilePath.append("_app.xml");

        AZ_Assert(applicationFilePath.size() <= _MAX_PATH, "Application path longer than expected");
        qstrcpy(m_applicationFilePath, applicationFilePath.c_str());

        // load all application entities, if present:
        AZ::IO::SystemFile cfg;

        if (cfg.Open(m_applicationFilePath, AZ::IO::SystemFile::SF_OPEN_READ_ONLY))
        {
            AZ::IO::SystemFileStream stream(&cfg, false);
            stream.Seek(0, AZ::IO::GenericStream::ST_SEEK_BEGIN);
            AZ::ObjectStream::LoadBlocking(&stream, *GetSerializeContext(),
                [this](void* classPtr, const AZ::Uuid& classId, const AZ::SerializeContext* sc)
                {
                    AZ::Entity* entity = sc->Cast<AZ::Entity*>(classPtr, classId);
                    if (entity)
                    {
                        m_applicationEntity = entity;
                    }
                });
            cfg.Close();
        }

        if (!m_applicationEntity)
        {
            m_applicationEntity = aznew AZ::Entity("WoodpeckerApplicationEntity");
        }

        CreateApplicationComponents();

        m_applicationEntity->InvalidateDependencies();
        m_applicationEntity->Init();
        m_applicationEntity->Activate();

        OnApplicationEntityActivated();
    }

    void Application::OnApplicationEntityActivated()
    {
    }

    bool Application::IsAppConfigWritable()
    {
        return !AZ::IO::SystemFile::Exists(m_applicationFilePath) || AZ::IO::SystemFile::IsWritable(m_applicationFilePath);
    }

    void Application::OnProjectSet(const char* projectPath)
    {
        (void)projectPath;
        CreateApplicationComponent();
    }

    void Application::RunAssetProcessor()
    {
        return;
    }

    void Application::SaveApplicationEntity()
    {
        if (!m_applicationEntity)
        {
            return;
        }

        // write the current applicaiton entity:
        AZStd::string applicationFilePath;
        AzFramework::StringFunc::Path::Join(GetExecutableFolder(), appName(), applicationFilePath);
        applicationFilePath += "_app.xml";

        QFileInfo fileAttribs(applicationFilePath.c_str());
        bool writeIt = true;
        if (fileAttribs.exists())
        {
            // file is found.
            if (fileAttribs.isHidden() || !fileAttribs.isWritable())
            {
                writeIt = false;
            }
        }

        if (writeIt)
        {
            using namespace AZ;
            AZStd::string tmpFileName(applicationFilePath);
            tmpFileName += ".tmp";

            IO::VirtualStream* fileStream = IO::Streamer::Instance().RegisterFileStream(tmpFileName.c_str(), IO::OpenMode::ModeWrite, false);
            if (fileStream == nullptr)
            {
                return;
            }

            AZ::SerializeContext* serializeContext = GetSerializeContext();
            IO::StreamerStream stream(fileStream, false);
            AZ_Assert(serializeContext, "ComponentApplication::m_serializeContext is NULL!");
            ObjectStream* objStream = ObjectStream::Create(&stream, *serializeContext, ObjectStream::ST_XML);

            bool entityWriteOk = objStream->WriteClass(m_applicationEntity);
            AZ_Warning("ComponentApplication", entityWriteOk, "Failed to write application entity to application file %s!", applicationFilePath.c_str());
            bool flushOk = objStream->Finalize();
            AZ_Warning("ComponentApplication", flushOk, "Failed finalizing application file %s!", applicationFilePath.c_str());
            IO::Streamer::Instance().UnRegisterStream(fileStream);

            if (entityWriteOk && flushOk)
            {
                if (IO::SystemFile::Rename(tmpFileName.c_str(), applicationFilePath.c_str(), true))
                {
                    return;
                }
                AZ_Warning("ComponentApplication", false, "Failed to rename %s to %s.", tmpFileName.c_str(), applicationFilePath.c_str());
            }
        }
    }

    void Application::CreateApplicationComponents()
    {
        EnsureComponentCreated(AzFramework::TargetManagementComponent::RTTI_Type());
        EnsureComponentCreated(AzFramework::DrillerNetworkConsoleComponent::RTTI_Type());
        EnsureComponentCreated(AzFramework::DrillerNetworkAgentComponent::RTTI_Type());
        EnsureComponentCreated(AzFramework::BootstrapReaderComponent::RTTI_Type());
    }

    void Application::CreateSystemComponents()
    {
        EnsureComponentCreated(AZ::MemoryComponent::RTTI_Type());
        EnsureComponentCreated(AZ::JobManagerComponent::RTTI_Type());
        EnsureComponentCreated(AZ::StreamerComponent::RTTI_Type());
        EnsureComponentCreated(AZ::ObjectStreamComponent::RTTI_Type());

        AZ_Assert(!m_desc.m_enableProjectManager || m_desc.m_enableGUI, "Enabling the project manager in the application settings requires enabling the GUI as well.");

        // if we're a GUI APP we need the UI Framework component:
        if (m_desc.m_enableGUI)
        {
            EnsureComponentCreated(AzToolsFramework::Framework::RTTI_Type());
        }
        else
        {
            EnsureComponentCreated(AzToolsFramework::Framework::RTTI_Type());
        }
    }

    void Application::ReflectSerialize()
    {
        AZ::SerializeContext* serializeContext = GetSerializeContext();
        if (serializeContext->GetEditContext() == nullptr)
        {
            serializeContext->CreateEditContext(); // we are the editor make a serialize context.
        }
        AZ::ComponentApplication::ReflectSerialize();

        ReflectSerializeDeprecated();
    }

    //=========================================================================
    // Create
    // [6/18/2012]
    //=========================================================================
    AZ::Entity*
    Application::Create(const char* systemEntityFileName, const StartupParameters& startupParameters)
    {
        if ((systemEntityFileName) && AZ::IO::SystemFile::Exists(systemEntityFileName))
        {
            // check if filename is valid

            return ComponentApplication::Create(systemEntityFileName, startupParameters);
        }
        else
        {
            AZ::ComponentApplication::Descriptor appDesc;

            AZ::Entity* success = ComponentApplication::Create(appDesc, startupParameters);
            return success;
        }
    }

    void Application::Destroy()
    {
        AZ::ComponentApplication::Destroy();
    }


    //=========================================================================
    // RegisterCoreComponents
    // [6/18/2012]
    //=========================================================================
    void Application::RegisterCoreComponents()
    {
        ComponentApplication::RegisterCoreComponents();

        RegisterComponentDescriptor(AzFramework::BootstrapReaderComponent::CreateDescriptor());
        RegisterComponentDescriptor(AzFramework::TargetManagementComponent::CreateDescriptor());
        RegisterComponentDescriptor(AzFramework::DrillerNetworkConsoleComponent::CreateDescriptor());
        RegisterComponentDescriptor(AzFramework::DrillerNetworkAgentComponent::CreateDescriptor());
        RegisterComponentDescriptor(AzToolsFramework::Framework::CreateDescriptor());
    }

    //=========================================================================
    // ReflectSerializeDeprecated
    // [11/12/2012]
    //=========================================================================
    void Application::ReflectSerializeDeprecated()
    {
        // A "convenient" function to place all high level (no other place to put) deprecated reflections.
        // IMPORTANT: Please to time stamp each deprecation so we can remove after some time (we know all
        // old data had been converted)
        GetSerializeContext()->ClassDeprecate("AssetDBComponent", "{397BAF44-3504-4a3e-BF96-C60F377313C7}"); // 5/21/2014
    }
}
