/*
  This file is part of GammaRay, the Qt application inspection and
  manipulation tool.

  Copyright (C) 2010-2011 Klarälvdalens Datakonsult AB, a KDAB Group company, info@kdab.com
  Author: Kevin Funk <kevin.funk@kdab.com>
  Author: Volker Krause <volker.krause@kdab.com>

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 2 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "vtkwidget.h"

#include "include/util.h"

#include <QTimer>

#include <vtk/vtkRenderer.h>
#include <vtk/vtkRenderWindow.h>
#include <vtk/vtkRenderWindowInteractor.h>
#include <vtk/vtkPolyDataMapper.h>
#include <vtk/vtkMutableDirectedGraph.h>
#include <vtk/vtkSmartPointer.h>
#include <vtk/vtkGraphLayout.h>
#include <vtk/vtkGraphToPolyData.h>
#include <vtk/vtkGlyphSource2D.h>
#include <vtk/vtkGlyph3D.h>
#include <vtk/vtkGraphLayoutView.h>
#include <vtk/vtkMutableUndirectedGraph.h>
#include <vtk/vtkStringArray.h>
#include <vtk/vtkDataSetAttributes.h>
#include <vtk/vtkVariantArray.h>
#include <vtk/vtkInteractorStyleTrackballCamera.h>
#include <vtk/vtkVertexListIterator.h>
#include <vtk/vtkIdTypeArray.h>
#include <vtk/vtkLookupTable.h>
#include <vtk/vtkViewTheme.h>

#include <iostream>

using namespace GammaRay;

//#define WITH_DEBUG

#ifdef WITH_DEBUG
#define DEBUG(msg) std::cout << Q_FUNC_INFO << " " << msg << std::endl;
#else
#define DEBUG(msg) qt_noop();
#endif

#define VTK_CREATE(type, name) \
  vtkSmartPointer<type> name = vtkSmartPointer<type>::New()

VtkWidget::VtkWidget(QWidget *parent)
  : QVTKWidget(parent),
    m_mousePressed(false),
    m_updateTimer(new QTimer(this)),
    m_objectFilter(0),
    m_colorIndex(0)
{
  setupRenderer();
  setupGraph();
  show();

  m_updateTimer->setInterval(0);
  m_updateTimer->setSingleShot(true);
  connect(m_updateTimer, SIGNAL(timeout()), SLOT(renderViewImpl()));
}

VtkWidget::~VtkWidget()
{
  clear();

  DEBUG("")
}

void VtkWidget::setupRenderer()
{
}

void VtkWidget::resetCamera()
{
  m_view->ResetCamera();
}

void VtkWidget::mousePressEvent(QMouseEvent *event)
{
  m_mousePressed = true;

  QVTKWidget::mousePressEvent(event);
}

void VtkWidget::mouseReleaseEvent(QMouseEvent *event)
{
  m_mousePressed = false;

  QVTKWidget::mouseReleaseEvent(event);
}

void VtkWidget::setupGraph()
{
  DEBUG("start")

  VTK_CREATE(vtkMutableDirectedGraph, graph);
  m_graph = graph;

  VTK_CREATE(vtkVariantArray, vertexPropertyArr);
  vertexPropertyArr->SetNumberOfValues(3);
  m_vertexPropertyArr = vertexPropertyArr;

  VTK_CREATE(vtkStringArray, vertexProp0Array);
  vertexProp0Array->SetName("labels");
  m_graph->GetVertexData()->AddArray(vertexProp0Array);

  // currently not used
  VTK_CREATE(vtkIntArray, vertexProp1Array);
  vertexProp1Array->SetName("weight");
  m_graph->GetVertexData()->AddArray(vertexProp1Array);

  // coloring
  vtkSmartPointer<vtkIntArray> vertexColors = vtkSmartPointer<vtkIntArray>::New();
  vertexColors->SetName("Color");
  m_graph->GetVertexData()->AddArray(vertexColors);

  vtkSmartPointer<vtkLookupTable> colorLookupTable = vtkSmartPointer<vtkLookupTable>::New();
  colorLookupTable->Build();

  vtkSmartPointer<vtkViewTheme> theme = vtkSmartPointer<vtkViewTheme>::New();
  theme->SetPointLookupTable(colorLookupTable);

  vtkGraphLayoutView *graphLayoutView = vtkGraphLayoutView::New();
  graphLayoutView->AddRepresentationFromInput(graph);
  graphLayoutView->SetVertexLabelVisibility(true);
  graphLayoutView->SetVertexLabelArrayName("labels");
  graphLayoutView->SetLayoutStrategyToSpanTree();
  graphLayoutView->SetVertexColorArrayName("Color");
  graphLayoutView->SetColorVertices(true);
  graphLayoutView->ApplyViewTheme(theme);
  m_view = graphLayoutView;

  VTK_CREATE(vtkInteractorStyleTrackballCamera, style);

  vtkSmartPointer<QVTKInteractor> renderWindowInteractor = vtkSmartPointer<QVTKInteractor>::New();
  renderWindowInteractor->SetRenderWindow(graphLayoutView->GetRenderWindow());
  renderWindowInteractor->SetInteractorStyle(style);
  renderWindowInteractor->Initialize();
  SetRenderWindow(graphLayoutView->GetRenderWindow());

  // code for generating edge arrow heads, needs some love
  // currently it modifies the layouting
  // how to use:
  // comment the AddRepresentationFromInput call to vtkGraphLayoutView and uncomment this
#if 0
  VTK_CREATE(vtkGraphLayout, layout);
  layout->SetInput(graph);
  layout->SetLayoutStrategy(strategy);

  // Tell the view to use the vertex layout we provide
  graphLayoutView->SetLayoutStrategyToPassThrough();
  // The arrows will be positioned on a straight line between two
  // vertices so tell the view not to draw arcs for parallel edges
  graphLayoutView->SetEdgeLayoutStrategyToPassThrough();

  // Add the graph to the view. This will render vertices and edges,
  // but not edge arrows.
  graphLayoutView->AddRepresentationFromInputConnection(layout->GetOutputPort());

  // Manually create an actor containing the glyphed arrows.
  VTK_CREATE(vtkGraphToPolyData, graphToPoly);
  graphToPoly->SetInputConnection(layout->GetOutputPort());
  graphToPoly->EdgeGlyphOutputOn();

  // Set the position (0: edge start, 1: edge end) where
  // the edge arrows should go.
  graphToPoly->SetEdgeGlyphPosition(0.98);

  // Make a simple edge arrow for glyphing.
  VTK_CREATE(vtkGlyphSource2D, arrowSource);
  arrowSource->SetGlyphTypeToEdgeArrow();
  arrowSource->SetScale(0.001);
  arrowSource->Update();

  // Use Glyph3D to repeat the glyph on all edges.
  VTK_CREATE(vtkGlyph3D, arrowGlyph);
  arrowGlyph->SetInputConnection(0, graphToPoly->GetOutputPort(1));
  arrowGlyph->SetInputConnection(1, arrowSource->GetOutputPort());

  // Add the edge arrow actor to the view.
  VTK_CREATE(vtkPolyDataMapper, arrowMapper);
  arrowMapper->SetInputConnection(arrowGlyph->GetOutputPort());
  VTK_CREATE(vtkActor, arrowActor);
  arrowActor->SetMapper(arrowMapper);
  graphLayoutView->GetRenderer()->AddActor(arrowActor);
#endif

  graphLayoutView->ResetCamera();
  graphLayoutView->Render();
  graphLayoutView->GetInteractor()->Start();

  DEBUG("end")
}

bool VtkWidget::addObject(QObject *object)
{
  m_availableObjects << object;

  return addObjectInternal(object);
}

bool VtkWidget::addObjectInternal(QObject *object)
{
  // ignore new objects during scene interaction
  // TODO: Add some code to add the objects later on => queue objects
  if (m_mousePressed) {
    DEBUG("Ignoring new object during scene interaction: "
          << object
          << " "
          << object->metaObject()->className())
    return false;
  }

  const QString className = QLatin1String(object->metaObject()->className());
  if (className == "QVTKInteractorInternal") {
    return false;
  }

  if (m_objectIdMap.contains(object)) {
    return false;
  }

  if (!filterAcceptsObject(object)) {
    return false;
  }

  const QString label = Util::displayString(object);
  const int weight = 1; // TODO: Make weight somewhat usable?
  m_vertexPropertyArr->SetValue(0, vtkUnicodeString::from_utf16(label.utf16()));
  m_vertexPropertyArr->SetValue(1, weight);
  static int colorIndex = 0;
  colorIndex = colorIndex % 10;

  QMap< QString, int >::const_iterator it = m_typeColorMap.constFind(className);
  if (it != m_typeColorMap.constEnd()) {
    m_vertexPropertyArr->SetValue(2, it.value());
  } else {
    m_vertexPropertyArr->SetValue(2, m_colorIndex);
    m_typeColorMap.insert(className, m_colorIndex);
    ++m_colorIndex;
  }

  const vtkIdType type = m_graph->AddVertex(m_vertexPropertyArr);
  DEBUG("Add: " << type << " " << object->metaObject()->className())
  m_objectIdMap[object] = type;

  QObject *parentObject = object->parent();
  if (parentObject) {
    if (!m_objectIdMap.contains(parentObject)) {
      addObject(parentObject);
    }
    if (m_objectIdMap.contains(parentObject)) {
      const vtkIdType parentType = m_objectIdMap[parentObject];
      m_graph->AddEdge(parentType, type);
    }
  }

  renderView();
  return true;
}

bool VtkWidget::removeObject(QObject *object)
{
  m_availableObjects.remove(object);

  return removeObjectInternal(object);
}

bool VtkWidget::removeObjectInternal(QObject *object)
{
  if (!m_objectIdMap.contains(object)) {
    return false;
  }

  // Remove id-for-object from VTK's graph data structure
  const vtkIdType type = m_objectIdMap[object];
  const int size = m_graph->GetNumberOfVertices();
  m_graph->RemoveVertex(type);

  // VTK re-orders the vertex IDs after removal!
  // we have to copy this behavior to track the associated QObject instances
  const vtkIdType lastId = m_objectIdMap.size() - 1;
  DEBUG("Type: " << type << " Last: " << lastId)
  if (type != lastId) {
    QObject *lastObject = m_objectIdMap.key(lastId);
    Q_ASSERT(lastObject);
    m_objectIdMap[lastObject] = type;
  }

  // Remove object from our map
  if (size > m_graph->GetNumberOfVertices()) {
    const bool count = m_objectIdMap.remove(object);
    Q_ASSERT(count == 1);
  } else {
    DEBUG("Warning: Should not happen: Could not remove vertice with id: " << type)
  }

  renderView();
  return true;
}

/// Schedules the re-rendering of the VTK view
void VtkWidget::renderView()
{
  m_updateTimer->start();
}

void VtkWidget::clear()
{
  // TODO: there must be an easier/faster way to clean the graph data
  // Just re-create the vtk graph data object?
  Q_FOREACH (QObject *object, m_objectIdMap.keys()) {
    removeObjectInternal(object);
  }
  m_objectIdMap.clear();

  renderView();
}

void VtkWidget::renderViewImpl()
{
  DEBUG("")

  m_view->Render();
  m_view->ResetCamera();
}

void VtkWidget::setObjectFilter(QObject *object)
{
  if (m_objectFilter == object) {
    return;
  }

  m_objectFilter = object;
  repopulate();
  resetCamera();
}

void VtkWidget::repopulate()
{
  DEBUG("")

  clear();

  Q_FOREACH (QObject *object, m_availableObjects) {
    addObject(object);
  }
}

// TODO: Move to Util.h?
static bool descendantOf(QObject *ascendant, QObject *obj)
{
  QObject *parent = obj->parent();
  if (!parent) {
    return false;
  }
  if (parent == ascendant) {
    return true;
  }
  return descendantOf(ascendant, parent);
}

bool VtkWidget::filterAcceptsObject(QObject *object) const
{
  if (m_objectFilter) {
    if (object == m_objectFilter) {
      return true;
    } else if (descendantOf(m_objectFilter, object)) {
      return true;
    } else {
      return false;
    }
  }
  return true;
}

#include "vtkwidget.moc"
