#ifndef POINCARE_TREE_REFERENCE_H
#define POINCARE_TREE_REFERENCE_H

#include "tree_pool.h"
#include <stdio.h>

namespace Poincare {

class TreeReference {
  friend class TreeNode;
  friend class AdditionNode; //TODO remove?
  friend class ExpressionNode;// TODO remove?
public:
  TreeReference(const TreeReference & tr) : m_identifier(TreePool::NoNodeIdentifier) { setTo(tr); }
  TreeReference(TreeReference&& tr) : m_identifier(TreePool::NoNodeIdentifier) { setTo(tr); }
  TreeReference& operator=(const TreeReference& tr) {
    setTo(tr);
    return *this;
  }
  TreeReference& operator=(TreeReference&& tr) {
    setTo(tr);
    return *this;
  }

  inline bool operator==(TreeReference t) { return m_identifier == t.identifier(); }

  TreeReference treeClone() const;
  ~TreeReference();

  int identifier() const { return m_identifier; }
  virtual TreeNode * node() const { return TreePool::sharedPool()->node(m_identifier); }

  bool isDefined() const { return m_identifier != TreePool::NoNodeIdentifier && node() != nullptr; }
  bool isAllocationFailure() const { return isDefined() && node()->isAllocationFailure(); }

  int nodeRetainCount() const {
    assert(isDefined());
    return node()->retainCount();
  }
  void incrementNumberOfChildren(int increment = 1) {
    assert(isDefined());
    node()->incrementNumberOfChildren(increment);
  }
  void decrementNumberOfChildren(int decrement = 1) {
    assert(isDefined());
    node()->decrementNumberOfChildren(decrement);
  }
  int numberOfDescendants(bool includeSelf) const {
    assert(isDefined());
    return node()->numberOfDescendants(includeSelf);
  }

  // Hierarchy
  bool hasChild(TreeReference t) const {
    assert(isDefined());
    return node()->hasChild(t.node());
  }
  bool hasSibling(TreeReference t) const {
    assert(isDefined());
    return node()->hasSibling(t.node());
  }
  bool hasAncestor(TreeReference t, bool includeSelf) const {
    assert(isDefined());
    return node()->hasAncestor(t.node(), includeSelf);
  }
  int numberOfChildren() const {
    assert(isDefined());
    return node()->numberOfChildren();
  }
  TreeReference parent() const {
    assert(isDefined());
    return TreeReference(node()->parent());
  }
  TreeReference treeChildAtIndex(int i) const {
    assert(isDefined());
    return TreeReference(node()->childAtIndex(i));
  }
  int indexOfChild(TreeReference t) const {
    assert(isDefined());
    return node()->indexOfChild(t.node());
  }

  // Hierarchy operations

  void addChildTreeAtIndex(TreeReference t, int index, int currentNumberOfChildren);
  void removeTreeChildAtIndex(int i);
  void removeTreeChild(TreeReference t, int childNumberOfChildren);
  void removeChildren();
  void replaceWith(TreeReference t);
  void replaceTreeChild(TreeReference oldChild, TreeReference newChild);
  void replaceTreeChildAtIndex(int oldChildIndex, TreeReference newChild) {
    assert(oldChildIndex >= 0 && oldChildIndex < numberOfChildren());
    TreeReference oldChild = treeChildAtIndex(oldChildIndex);
    replaceTreeChild(oldChild, newChild);
  }
  void replaceWithAllocationFailure(int currentNumberOfChildren);
  void mergeTreeChildrenAtIndex(TreeReference t, int i);
  TreeReference(const TreeNode * node) { // TODO Make this protected
    if (node == nullptr) {
      m_identifier = TreePool::NoNodeIdentifier;
    } else {
      setIdentifierAndRetain(node->identifier());
    }
  }

protected:
  TreeReference() : m_identifier(-1) {}
  void setIdentifierAndRetain(int newId) {
    m_identifier = newId;
    node()->retain();
  }
  void setTo(const TreeReference & tr);
  int m_identifier;
};

typedef TreeReference TreeRef;

}

#endif
