<template>
  <div>
    <div class="toolbar">
      <div class="toolbar-left">
        <button class="chip" :class="{ active: hallStore.tagFilter === '' }" @click="hallStore.tagFilter = ''; hallStore.loadPosts()">全部</button>
        <button v-for="tag in hallStore.tags" :key="tag.name" class="chip" :class="{ active: hallStore.tagFilter === tag.name }" @click="hallStore.tagFilter = tag.name; hallStore.loadPosts()">
          {{ tag.name }} ({{ tag.count }})
        </button>
      </div>
      <div style="display:flex;gap:8px">
        <button class="chip" :class="{ active: hallStore.sort === 'latest' }" @click="hallStore.sort = 'latest'; hallStore.loadPosts()">最新</button>
        <button class="chip" :class="{ active: hallStore.sort === 'popular' }" @click="hallStore.sort = 'popular'; hallStore.loadPosts()">最热</button>
      </div>
    </div>
    <div class="masonry" v-if="hallStore.posts.length > 0">
      <div v-for="post in hallStore.posts" :key="post.id" class="masonry-card">
        <img :src="post.image_url + '?token=' + token" :alt="post.title" loading="lazy" />
        <div class="masonry-content">
          <h3>{{ post.title || post.filename }}</h3>
          <p>@{{ post.username }}</p>
          <div class="masonry-meta">
            <span><i class="fas fa-heart"></i> {{ post.likes }}</span>
            <span><i class="fas fa-eye"></i> {{ post.views }}</span>
          </div>
        </div>
      </div>
    </div>
    <div v-else class="empty-state">
      <div class="empty-icon"><i class="fas fa-images"></i></div>
      <h3>大厅暂无内容</h3>
      <p>成为第一个发布图片的用户</p>
    </div>
  </div>
</template>

<script setup>
import { onMounted } from 'vue'
import { useHallStore } from '../stores/hall'
import { useAppStore } from '../stores/app'

const hallStore = useHallStore()
const appStore = useAppStore()
const token = appStore.token

onMounted(() => { hallStore.loadPosts(); hallStore.loadTags() })
</script>

<style scoped>
.toolbar { display: flex; justify-content: space-between; align-items: center; margin-bottom: 20px; flex-wrap: wrap; gap: 12px; }
.toolbar-left { display: flex; gap: 10px; flex-wrap: wrap; }
.chip { height: 40px; padding: 0 16px; border-radius: 12px; background: white; display: flex; align-items: center; gap: 8px; font-size: 13px; font-weight: 700; cursor: pointer; border: 1px solid #e5e7eb; }
.chip:hover { transform: translateY(-2px); }
.chip.active { background: #2563eb; color: white; border-color: #2563eb; }
.masonry { columns: 3 320px; column-gap: 22px; margin-top: 24px; }
.masonry-card { break-inside: avoid; margin-bottom: 22px; background: white; border-radius: 28px; overflow: hidden; box-shadow: 0 12px 40px rgba(15,23,42,.05); transition: .2s; }
.masonry-card:hover { transform: translateY(-4px); }
.masonry-card img { width: 100%; display: block; }
.masonry-content { padding: 20px; }
.masonry-content h3 { font-size: 18px; font-weight: 800; }
.masonry-content p { margin-top: 10px; color: #6b7280; font-size: 13px; }
.masonry-meta { margin-top: 16px; display: flex; gap: 16px; font-size: 13px; color: #6b7280; }
.masonry-meta i { margin-right: 4px; }
.empty-state { text-align: center; padding: 80px 20px; }
.empty-icon { width: 80px; height: 80px; border-radius: 50%; background: #eff6ff; display: inline-flex; align-items: center; justify-content: center; margin-bottom: 20px; }
.empty-icon i { font-size: 2rem; color: #2563eb; opacity: .6; }
@media(max-width: 1200px) { .masonry { columns: 2; } }
@media(max-width: 768px) { .masonry { columns: 1; } }
</style>
