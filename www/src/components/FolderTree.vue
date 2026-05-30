<template>
  <div class="folder-tree">
    <div class="tree-header">
      <span class="tree-title">文件夹</span>
      <button class="tree-add" @click="$emit('create')" title="新建文件夹">
        <i class="fas fa-plus"></i>
      </button>
    </div>
    <div class="tree-list">
      <div class="tree-item" :class="{ active: !currentFolderId }" @click="$emit('select', null)">
        <i class="fas fa-folder-open"></i>
        <span>全部文件</span>
      </div>
      <FolderTreeNode
        v-for="folder in tree"
        :key="folder.id"
        :folder="folder"
        :current-id="currentFolderId"
        @select="$emit('select', $event)"
        @create="$emit('create', $event)"
        @rename="$emit('rename', $event)"
        @delete="$emit('delete', $event)"
      />
    </div>
  </div>
</template>

<script setup>
import FolderTreeNode from './FolderTreeNode.vue'

defineProps({
  tree: { type: Array, default: () => [] },
  currentFolderId: { type: Number, default: null }
})

defineEmits(['select', 'create', 'rename', 'delete'])
</script>

<style scoped>
.folder-tree { margin-top: 28px; }
.tree-header { display: flex; justify-content: space-between; align-items: center; padding: 0 12px; margin-bottom: 14px; }
.tree-title { font-size: 12px; font-weight: 700; color: #9ca3af; text-transform: uppercase; letter-spacing: 0.5px; }
.tree-add { width: 24px; height: 24px; border-radius: 6px; border: none; background: transparent; color: #9ca3af; cursor: pointer; display: flex; align-items: center; justify-content: center; font-size: 12px; }
.tree-add:hover { background: #f3f4f6; color: #2563eb; }
.tree-list { display: flex; flex-direction: column; gap: 2px; }
.tree-item {
  display: flex; align-items: center; gap: 10px; padding: 10px 12px;
  border-radius: 12px; cursor: pointer; font-size: 14px; font-weight: 500;
  color: #374151; transition: all 0.15s;
}
.tree-item:hover { background: #f5f7fb; }
.tree-item.active { background: linear-gradient(135deg, rgba(37,99,235,.1), rgba(124,58,237,.08)); color: #2563eb; }
.tree-item i { font-size: 14px; width: 18px; text-align: center; }
.tree-item .count { margin-left: auto; font-size: 12px; color: #9ca3af; }
</style>
