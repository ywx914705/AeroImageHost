import { defineStore } from 'pinia'
import { ref, computed } from 'vue'

export const useWorkspaceStore = defineStore('workspace', () => {
  const workspaces = ref([
    { id: 1, name: 'Personal', icon: 'folder', color: '#2563eb', is_default: true },
    { id: 2, name: 'Team', icon: 'users', color: '#7c3aed', is_default: false },
    { id: 3, name: 'Client Assets', icon: 'briefcase', color: '#10b981', is_default: false },
  ])
  const currentWorkspace = ref(workspaces.value[0])

  function switchWorkspace(ws) {
    currentWorkspace.value = ws
  }

  function createWorkspace(name) {
    const ws = { id: Date.now(), name, icon: 'folder', color: '#6b7280', is_default: false }
    workspaces.value.push(ws)
    return ws
  }

  function deleteWorkspace(id) {
    workspaces.value = workspaces.value.filter(w => w.id !== id)
    if (currentWorkspace.value.id === id) {
      currentWorkspace.value = workspaces.value[0]
    }
  }

  return { workspaces, currentWorkspace, switchWorkspace, createWorkspace, deleteWorkspace }
})
