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
#pragma once

#include <QGraphicsLinearLayout>
#include <qgraphicswidget.h>

#include <AzCore/Component/Component.h>
#include <AzCore/Component/EntityBus.h>

#include <Components/Nodes/NodeLayoutComponent.h>
#include <GraphCanvas/Components/Nodes/NodeLayoutBus.h>
#include <GraphCanvas/Components/Nodes/Wrapper/WrapperNodeBus.h>
#include <GraphCanvas/Components/StyleBus.h>
#include <GraphCanvas/Styling/StyleHelper.h>

class QGraphicsGridLayout;

namespace GraphCanvas
{
    class GraphCanvasLabel;

    //! Lays out the parts of the generic Node
    class WrapperNodeLayoutComponent
        : public NodeLayoutComponent
        , public NodeNotificationBus::MultiHandler
        , public WrapperNodeRequestBus::Handler
        , public StyleNotificationBus::Handler
    {
    private:
        
        typedef AZStd::unordered_map< AZ::EntityId, WrappedNodeConfiguration > WrappedNodeConfigurationMap;
        
        class WrappedNodeConfigurationComparator
        {
        public:
            WrappedNodeConfigurationComparator()
                : m_configurations(nullptr)
            {
                AZ_Assert(false, "Trying to use invalid ConfigurationComparator");
            }
            
            WrappedNodeConfigurationComparator(const WrappedNodeConfigurationMap* configurationMap)
                : m_configurations(configurationMap)
            {
            }

            bool operator()(const AZ::EntityId& a, const AZ::EntityId& b)
            {
                return m_configurations->at(a) < m_configurations->at(b);
            }
            
        private:
            const WrappedNodeConfigurationMap* m_configurations;
        };
        
        typedef AZStd::set<AZ::EntityId, WrappedNodeConfigurationComparator> WrappedNodeSet;
    
        class WrappedNodeLayout
            : public QGraphicsWidget
        {
        public:
            AZ_CLASS_ALLOCATOR(WrappedNodeLayout, AZ::SystemAllocator, 0);
            WrappedNodeLayout(WrapperNodeLayoutComponent& wrapperLayoutComponent);
            ~WrappedNodeLayout();
            
            void RefreshStyle();
            void RefreshLayout();
            void ClearLayout();
        
        private:
        
            void LayoutItems();
        
            WrapperNodeLayoutComponent& m_wrapperLayoutComponent;
            Styling::StyleHelper m_styleHelper;

            QGraphicsLinearLayout* m_layout;
        };

        class WrappedNodeActionGraphicsWidget
            : public QGraphicsWidget
        {
        public:
            enum class StyleState
            {
                Empty,
                HasElements,
            };

            AZ_CLASS_ALLOCATOR(WrappedNodeActionGraphicsWidget, AZ::SystemAllocator, 0);
            WrappedNodeActionGraphicsWidget(WrapperNodeLayoutComponent& wrapperLayoutComponent);

            void OnAddedToScene();

            void RefreshStyle();
            void SetActionString(const QString& addNodeString);

            void SetStyleState(StyleState state);

        private:

            bool sceneEventFilter(QGraphicsItem* watched, QEvent* event) override;

            bool m_acceptDrop;

            WrapperNodeLayoutComponent& m_wrapperLayoutComponent;
            StyleState m_styleState;
            GraphCanvasLabel* m_displayLabel;
        };

        friend class AddWrappedNodeGraphicsWidget;
        friend class WrappedNodeLayout;
        
    public:
        AZ_COMPONENT(WrapperNodeLayoutComponent, "{15A56424-0846-45D7-A4C2-ADCAE3E98DE0}", NodeLayoutComponent);
        static void Reflect(AZ::ReflectContext*);
        
        static AZ::Entity* CreateWrapperNodeEntity(const char* nodeType);

        WrapperNodeLayoutComponent();
        ~WrapperNodeLayoutComponent() override;

        // AZ::Component
        static void GetProvidedServices(AZ::ComponentDescriptor::DependencyArrayType& provided)
        {
            provided.push_back(NodeLayoutServiceCrc);
            provided.push_back(WrapperNodeLayoutServiceCrc);
        }

        static void GetIncompatibleServices(AZ::ComponentDescriptor::DependencyArrayType& incompatible)
        {
            incompatible.push_back(NodeLayoutServiceCrc);
            incompatible.push_back(WrapperNodeLayoutServiceCrc);
        }

        static void GetDependentServices(AZ::ComponentDescriptor::DependencyArrayType& dependent)
        {
            dependent.push_back(AZ_CRC("GraphCanvas_NodeLayoutSupportService", 0xa8b639be));
            dependent.push_back(AZ_CRC("GraphCanvas_TitleService", 0xfe6d63bc));
            dependent.push_back(AZ_CRC("GraphCanvas_SlotsContainerService", 0x948b6696));
        }

        static void GetRequiredServices(AZ::ComponentDescriptor::DependencyArrayType& required)
        {
            required.push_back(AZ_CRC("GraphCanvas_NodeService", 0xcc0f32cc));
            required.push_back(AZ_CRC("GraphCanvas_StyledGraphicItemService", 0xeae4cdf4));
        }

        void Init();
        void Activate();
        void Deactivate();
        ////
        
        // WrapperNodeRequestBus
        void SetActionString(const QString& actionString) override;

        AZStd::vector< AZ::EntityId > GetWrappedNodeIds() const override;

        void WrapNode(const AZ::EntityId& nodeId, const WrappedNodeConfiguration& nodeConfiguration) override;
        void UnwrapNode(const AZ::EntityId& nodeId) override;

        void SetWrapperType(const AZ::Crc32& wrapperType) override;
        AZ::Crc32 GetWrapperType() const override;
        ////

        // NodeNotificationBus
        void OnNodeActivated() override;

        void OnNodeAboutToSerialize(GraphSerialization& sceneSerialization) override;
        void OnNodeDeserialized(const AZ::EntityId& graphId, const GraphSerialization& sceneSerialization) override;

        void OnAddedToScene(const AZ::EntityId& sceneId) override;
        void OnRemovedFromScene(const AZ::EntityId& sceneId) override;
        ////

        // StyleNotificationBus
        void OnStyleChanged() override;
        ////

    protected:

        void RefreshActionStyle();

        bool ShouldAcceptDrop(const QMimeData* mimeData) const;
        void OnDragLeave() const;
        void OnActionWidgetClicked(const QPointF& scenePoint, const QPoint& screenPoint) const;
    
        void ClearLayout();
        void CreateLayout();
        void UpdateLayout();
        
        void RefreshDisplay();

        Styling::StyleHelper m_styleHelper;

        AZ::Crc32                   m_wrapperType;

        AZ::u32                     m_elementCounter;
        WrappedNodeConfigurationMap m_wrappedNodeConfigurations;
        WrappedNodeSet              m_wrappedNodes;

        // Overall Layout        
        QGraphicsLayoutItem* m_title;
        QGraphicsLayoutItem* m_slots;
        
        WrappedNodeLayout* m_wrappedNodeLayout;
        WrappedNodeActionGraphicsWidget* m_wrapperNodeActionWidget;
    };
}