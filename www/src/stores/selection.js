import { defineStore } from 'pinia'
import { ref, computed } from 'vue'

export const useSelectionStore = defineStore('selection', () => {
  const selectedIds = ref(new Set())
  const selectionMode = ref(false)
  const lastSelectedIndex = ref(null)

  const selectedCount = computed(() => selectedIds.value.size)

  function toggleSelect(id) {
    if (selectedIds.value.has(id)) {
      selectedIds.value.delete(id)
    } else {
      selectedIds.value.add(id)
    }
    selectionMode.value = selectedIds.value.size > 0
  }

  function selectAll(files) {
    files.forEach(f => selectedIds.value.add(f.file_id))
    selectionMode.value = true
  }

  function selectNone() {
    selectedIds.value.clear()
    selectionMode.value = false
    lastSelectedIndex.value = null
  }

  function isSelected(id) {
    return selectedIds.value.has(id)
  }

  function getSelectedIds() {
    return Array.from(selectedIds.value)
  }

  return { selectedIds, selectionMode, lastSelectedIndex, selectedCount, toggleSelect, selectAll, selectNone, isSelected, getSelectedIds }
})
